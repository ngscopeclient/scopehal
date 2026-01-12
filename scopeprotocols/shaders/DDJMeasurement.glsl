/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
* All rights reserved.                                                                                                 *
*                                                                                                                      *
* Redistribution and use in source and binary forms, with or without modification, are permitted provided that the     *
* following conditions are met:                                                                                        *
*                                                                                                                      *
*    * Redistributions of source code must retain the above copyright notice, this list of conditions, and the         *
*      following disclaimer.                                                                                           *
*                                                                                                                      *
*    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the       *
*      following disclaimer in the documentation and/or other materials provided with the distribution.                *
*                                                                                                                      *
*    * Neither the name of the author nor the names of any contributors may be used to endorse or promote products     *
*      derived from this software without specific prior written permission.                                           *
*                                                                                                                      *
* THIS SOFTWARE IS PROVIDED BY THE AUTHORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED   *
* TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL *
* THE AUTHORS BE HELD LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES        *
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR       *
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE       *
* POSSIBILITY OF SUCH DAMAGE.                                                                                          *
*                                                                                                                      *
***********************************************************************************************************************/

#version 460
#pragma shader_stage(compute)

#extension GL_ARB_gpu_shader_int64 : require
#extension GL_EXT_shader_8bit_storage : require
#extension GL_EXT_shader_atomic_int64 : require
#extension GL_EXT_shader_atomic_float : require

layout(std430, binding=0) restrict readonly buffer buf_tieOffsets
{
	int64_t tieOffsets[];
};

layout(std430, binding=1) restrict readonly buffer buf_tieSamples
{
	float tieSamples[];
};

layout(std430, binding=2) restrict readonly buffer buf_dataOffsets
{
	int64_t dataOffsets[];
};

layout(std430, binding=3) restrict readonly buffer buf_dataDurations
{
	int64_t dataDurations[];
};

layout(std430, binding=4) restrict readonly buffer buf_dataValues
{
	uint8_t dataValues[];
};

layout(std430, binding=5) restrict buffer buf_numTable
{
	int64_t numTable[];
};

layout(std430, binding=6) restrict buffer buf_sumTable
{
	float sumTable[];
};

layout(std430, push_constant) uniform constants
{
	uint numDataSamples;
	uint numTieSamples;
};

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

void main()
{
	//Each thread processes an equal-sized group of edges in the sampled data signal
	uint numThreads = gl_NumWorkGroups.x * gl_WorkGroupSize.x;
	uint samplesPerThread = numDataSamples / numThreads;
	uint istart = gl_GlobalInvocationID.x * samplesPerThread;

	//Find end position
	uint iend = istart + samplesPerThread;
	if(gl_GlobalInvocationID.x == (numThreads - 1))
		iend = numDataSamples - 1;

	//If not the first block, back up by 8 samples so processing windows overlap
	if(istart != 0)
		istart -= 8;

	//Get the timestamp of our first edge
	int64_t target = dataOffsets[istart];

	//Binary search for the first golden clock edge after this sample
	uint pos = numTieSamples/2;
	uint last_lo = 0;
	uint last_hi = numTieSamples-1;
	uint iedge = 0;
	if(numTieSamples > 0)
	{
		//Clip if out of range
		if(tieOffsets[0] >= target)
			iedge = 0;
		else if(tieOffsets[last_hi] < target)
			iedge = numTieSamples - 1;

		//Main loop
		else
		{
			while(true)
			{
				//Stop if we've bracketed the target
				if( (last_hi - last_lo) <= 1)
				{
					iedge = last_lo;
					break;
				}

				//Move down
				if(tieOffsets[pos] > target)
				{
					uint delta = pos - last_lo;
					last_hi = pos;
					pos = last_lo + delta/2;
				}

				//Move up
				else
				{
					uint delta = last_hi - pos;
					last_lo = pos;
					pos = last_hi - delta/2;
				}
			}
		}
	}

	//We actually want the clock edge before the target, so decrement it unless it's the first in the capture
	if(iedge > 0)
		iedge --;

	uint nbits = 0;
	int64_t tfirst = tieOffsets[iedge];
	uint itie = iedge;
	uint window = 0;
	uint tielast = numTieSamples - 1;
	for(uint idata=istart; idata < iend; idata ++)
	{
		//Sample the next bit in the thresholded waveform
		window = (window >> 1);
		if(uint(dataValues[idata]) != 0)
			window |= 0x80;
		nbits ++;

		//need 8 in last_window, plus one more for the current bit
		if(nbits < 9)
			continue;

		//If we're still before the first TIE sample, nothing to do
		int64_t tstart = dataOffsets[idata];
		if(tstart < tfirst)
			continue;

		//Advance TIE samples if needed
		int64_t target = tieOffsets[itie];
		while( (target < tstart) && (itie < tielast) )
		{
			itie ++;
			target = tieOffsets[itie];
		}
		if(itie >= numTieSamples)
			break;

		//If the TIE sample is not in this bit, don't do anything.
		//We need edges within this UI.
		int64_t tend = tstart + dataDurations[idata];
		if(target > tend)
			continue;

		//Save the info in the DDJ table
		//TODO: integrate this in shared memory then only push to global at the end
		atomicAdd(numTable[window], 1);
		atomicAdd(sumTable[window], tieSamples[itie]);
	}
}
