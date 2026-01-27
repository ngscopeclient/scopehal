/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
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

#extension GL_EXT_shader_8bit_storage : require
#extension GL_ARB_gpu_shader_int64 : require

layout(std430, binding=0) restrict readonly buffer buf_offsetsIn
{
	int64_t offsetsIn[];
};

layout(std430, binding=1) restrict writeonly buffer buf_offsetsOut
{
	int64_t offsetsOut[];
};

layout(std430, binding=2) restrict writeonly buffer buf_durationsOut
{
	int64_t durationsOut[];
};

layout(std430, binding=3) restrict writeonly buffer buf_samplesOut
{
	uint8_t samplesOut[];
};

layout(std430, binding=4) restrict writeonly buffer buf_edgecount
{
	uint edgecount;
};

layout(std430, push_constant) uniform constants
{
	int64_t	halfui;
	int64_t timescale;
	uint numIndexes;
	uint numSamples;
	uint inputPerThread;
	uint outputPerThread;
	uint order;
};

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

/**
	@brief Final output merging and level crossings
 */
void main()
{
	//Figure out how many samples we had before us
	uint numThreads = gl_NumWorkGroups.x * gl_WorkGroupSize.x;
	bool lastBlock = (gl_GlobalInvocationID.x + 1) >= numThreads;
	uint writebase = 0;
	for(uint i=0; i<gl_GlobalInvocationID.x; i++)
		writebase += uint(offsetsIn[i * outputPerThread]);
	uint readbase = gl_GlobalInvocationID.x * outputPerThread;
	uint numSamples = uint(offsetsIn[readbase]);

	//Skip the sample counter itself
	readbase ++;

	//Do the actual copy
	for(uint i=0; i<numSamples; i++)
	{
		uint iin = readbase + i;
		uint iout = writebase + i;

		//Copy the offset
		int64_t offin = offsetsIn[iin];
		offsetsOut[iout] = offin;

		//Generate the squarewave
		samplesOut[iout] = uint8_t(iout % 2);

		//Next sample from this block? Easy peasy
		int64_t offNext = offin + 1;
		if(i+1 < numSamples)
			offNext = offsetsIn[iin + 1];

		//Is there another block? Use its first sample
		else if(!lastBlock)
			offNext = offsetsIn[readbase + outputPerThread];

		//otherwise this is the very last sample in the capture, just use the default duration of 1

		//Either way, write the duration
		durationsOut[iout] = offNext - offin;
	}

	//If this is the last block, write the number of samples
	if(lastBlock)
		edgecount = writebase + numSamples;
}
