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
#extension GL_EXT_debug_printf : enable

layout(std430, binding=0) restrict readonly buffer buf_samplesIn
{
	float samplesIn[];
};

layout(std430, binding=1) restrict writeonly buffer buf_offsetsOut
{
	int64_t offsetsOut[];
};

layout(std430, binding=2) restrict buffer buf_samplesOut
{
	float samplesOut[];
};

layout(std430, push_constant) uniform constants
{
	int64_t	timescale;
	int64_t	triggerPhase;
	uint	bufferPerThread;
	uint 	len;
	float 	vmin;
	float 	mid;
	float	range;
	float	global_base;
};

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

void main()
{
	//Initial starting sample indexes for this thread
	uint numThreads = gl_NumWorkGroups.x * gl_WorkGroupSize.x;
	uint numSamplesPerThread = len / numThreads;
	uint startpos = gl_GlobalInvocationID.x * numSamplesPerThread;

	//Adjust starting position to the left until we find a rising edge,
	//the start of the waveform, or the start of a previous block
	uint minpos;
	if(gl_GlobalInvocationID.x == 0)
		minpos = 0;
	else
		minpos = startpos - numSamplesPerThread;

	for(; startpos > minpos; startpos --)
	{
		if(samplesIn[startpos] > mid)
			break;
	}

	//Ending position
	uint endpos;
	if( (gl_GlobalInvocationID.x + 1) == numThreads)
		endpos = len;
	else
		endpos = (gl_GlobalInvocationID.x+1) * numSamplesPerThread;

	float delta = range * 0.1;
	int64_t tfall = 0;

	uint numOutputSamples = 0;
	uint outbase = bufferPerThread * gl_GlobalInvocationID.x;

	//Use samplesOut as a scratchpad buffer for averaging
	uint averageBufferStart = 0;
	uint averageBufferCount = 0;
	uint averageBufferEnd = outbase + bufferPerThread;

	bool first = (gl_GlobalInvocationID.x == 0);

	if(gl_GlobalInvocationID.x == 0)
	{
		debugPrintfEXT("global_base = %f, delta = %f\n", global_base, delta);
	}

	float last = samplesIn[startpos];
	for(uint i=startpos + 1; i < endpos; i++)
	{
		//Wait for a rising edge (end of the low period)
		float cur = samplesIn[i];
		int64_t tnow = int64_t(i) * timescale + triggerPhase;

		//Find falling edge
		if( (cur < mid) && (last >= mid) )
			tfall = tnow;

		//Find rising edge
		if( (cur > mid) && (last <= mid) )
		{
			//Done, add the sample
			if(averageBufferCount != 0)
			{
				if(first)
					first = false;

				else
				{
					//Average the middle 50% of the samples.
					//Discard beginning and end as they include parts of the edge
					float sum = 0;
					uint count = 0;

					uint skip = averageBufferCount / 4;
					uint start = averageBufferStart + skip;
					uint end = averageBufferStart + averageBufferCount - (skip + 1);
					for(uint j=start; j<=end; j++)
					{
						sum += samplesOut[j];
						count ++;
					}

					float vavg = sum / count;

					int64_t tmid = (tnow + tfall) / 2;

					//Sanity check that we actually averaged some samples, then append the sample
					if(start != end)
					{
						uint iout = outbase + numOutputSamples + 1;
						samplesOut[iout] = vavg;
						offsetsOut[iout] = tmid;
						numOutputSamples ++;
					}
				}

				averageBufferCount = 0;
			}
		}

		//If the value is fairly close to the calculated base, average it
		if(abs(cur - global_base) < delta)
		{
			//If output buffer is empty, use the current sample as the output index
			if(averageBufferCount == 0)
				averageBufferStart = outbase + numOutputSamples + 1;

			//Add to the list of samples to average
			uint idx = averageBufferStart + averageBufferCount;
			if(idx < averageBufferEnd)
			{
				samplesOut[idx] = cur;
				averageBufferCount ++;
			}
		}

		last = cur;
	}

	//Save number of samples at the start of the output buffer
	offsetsOut[outbase] = int64_t(numOutputSamples);
}
