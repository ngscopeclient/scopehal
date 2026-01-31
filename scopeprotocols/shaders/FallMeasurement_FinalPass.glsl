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

layout(std430, binding=0) restrict readonly buffer buf_offsetsIn
{
	int64_t offsetsIn[];
};

layout(std430, binding=1) restrict readonly buffer buf_samplesIn
{
	float samplesIn[];
};

layout(std430, binding=2) restrict writeonly buffer buf_offsets
{
	int64_t offsets[];
};

layout(std430, binding=3) restrict writeonly buffer buf_samplesOut
{
	float samplesOut[];
};

layout(std430, binding=4) restrict writeonly buffer buf_durations
{
	int64_t durations[];
};

layout(std430, binding=5) restrict writeonly buffer buf_finalCount
{
	int64_t finalCount[];
};

layout(std430, binding=6) restrict writeonly buffer buf_finalSum
{
	float finalSum[];
};

layout(std430, push_constant) uniform constants
{
	int64_t timescale;
	uint	len;
	uint	bufferPerThread;
	float	vstart;
	float	vend;
};

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

void main()
{
	//Initial starting sample indexes for this thread
	uint numThreads = gl_NumWorkGroups.x * gl_WorkGroupSize.x;
	uint nLastThread = numThreads - 1;

	//Get starting input and output indexes
	uint writebase = 0;
	for(uint i=0; i<gl_GlobalInvocationID.x; i++)
		writebase += uint(offsetsIn[i * bufferPerThread]);
	uint readbase = gl_GlobalInvocationID.x * bufferPerThread + 1;

	//See how many samples this block is copying
	uint count = uint(offsetsIn[readbase - 1]);

	//Temporaries for Kahan summation
	float partialSum = 0;
	float c = 0;

	//Copy samples
	for(uint i=0; i<count; i ++)
	{
		//Read input
		uint iin = readbase + i;
		uint iout = writebase + i;
		int64_t offset = offsetsIn[iin];
		float fin = samplesIn[iin];

		//Kahan sum the output values
		float y = fin - c;
		float t = partialSum + y;
		c = (t - partialSum) - y;
		partialSum = t;

		//Copy to output
		offsets[iout] = offset;
		samplesOut[iout] = fin;

		//Is there a sample after this one? Use it for duration
		if(i+1 < count)
			durations[iout] = offsetsIn[iin + 1] - offset;

		//No sample, but another block?
		else if(gl_GlobalInvocationID.x != nLastThread)
		{
			bool found = false;
			for(uint nblock=gl_GlobalInvocationID.x; nblock < numThreads; nblock ++)
			{
				//Make sure the subsequent thread has samples
				uint nextbase = (gl_GlobalInvocationID.x + 1) * bufferPerThread;
				uint nextCount = uint(offsetsIn[nextbase]);
				if(nextCount == 0)
					continue;

				//We're good, use this duration
				int64_t nextOffset = offsetsIn[nextbase + 1];
				durations[iout] = nextOffset - offset;
				found = true;
				break;
			}

			//No more samples? Tie it off
			if(!found)
				durations[iout] = 1;
		}

		//Last block?
		else
			durations[iout] = 1;
	}

	//Save final block index
	if(gl_GlobalInvocationID.x == nLastThread)
		finalCount[0] = int64_t(writebase + count);

	//Save partial sum for this block
	finalSum[gl_GlobalInvocationID.x] = partialSum;
}
