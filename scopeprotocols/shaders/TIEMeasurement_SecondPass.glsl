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

layout(std430, binding=0) restrict readonly buffer buf_firstPassOutput
{
	int64_t firstPassOutput[];
};

layout(std430, binding=1) restrict writeonly buffer buf_offsets
{
	int64_t offsets[];
};

layout(std430, binding=2) restrict writeonly buffer buf_durations
{
	int64_t durations[];
};

layout(std430, binding=3) restrict writeonly buffer buf_samples
{
	float samples[];
};

layout(std430, binding=4) restrict writeonly buffer buf_summary
{
	int64_t nsamplesOut;
};

layout(std430, push_constant) uniform constants
{
	int64_t	skip_time;
	uint	nedges;
	uint	ngolden;
	uint	blockBufferSize;
	uint	maxEdgesPerThread;
};

#define BLOCK_SIZE 64

layout(local_size_x=BLOCK_SIZE, local_size_y=1, local_size_z=1) in;

shared uint blockbase;
shared uint startingSum;

void main()
{
	//Initialize shared variables
	if(gl_LocalInvocationID.x == 0)
	{
		startingSum = 0;
		blockbase = gl_GlobalInvocationID.x;
	}
	barrier();
	memoryBarrierShared();

	//Find starting sample index for the block
	uint tmp = 0;
	for(uint i=0; i < blockbase; i += BLOCK_SIZE)
		tmp += uint(firstPassOutput[i * blockBufferSize]);
	atomicAdd(startingSum, tmp);
	barrier();
	memoryBarrierShared();

	uint writebase = startingSum;
	for(uint i=blockbase; i<gl_GlobalInvocationID.x; i++)
		writebase += uint(firstPassOutput[i * blockBufferSize]);

	//Copy samples
	uint readbase = gl_GlobalInvocationID.x * blockBufferSize;
	uint count = uint(firstPassOutput[readbase]);
	int64_t lastOffset = firstPassOutput[readbase + 1];
	uint iout = writebase;
	for(uint i=0; i<count; i++)
	{
		//Copy offset and sample data
		int64_t offset = firstPassOutput[readbase + i*2 + 1];
		offsets[iout] = offset;
		samples[iout] = float(firstPassOutput[readbase + i*2 + 2]);

		//Calculate duration
		if(iout > 0)
			durations[iout - 1] = offset - lastOffset;

		lastOffset = offset;
		iout ++;
	}

	//Set final duration to 1
	if(iout > 0)
		durations[iout - 1] = 1;

	//If we're the last thread, save the final sample count
	uint numThreads = gl_NumWorkGroups.x * gl_WorkGroupSize.x;
	if(gl_GlobalInvocationID.x == (numThreads - 1) )
		nsamplesOut = int64_t(iout);
}
