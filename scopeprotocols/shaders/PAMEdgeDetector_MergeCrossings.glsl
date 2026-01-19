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

layout(std430, binding=0) restrict readonly buffer buf_indexesIn
{
	uint idxIn[];
};

layout(std430, binding=1) restrict readonly buffer buf_statesIn
{
	uint8_t statesIn[];
};

layout(std430, binding=2) restrict readonly buffer buf_risingIn
{
	uint8_t risingIn[];
};

layout(std430, binding=3) restrict writeonly buffer buf_indexesOut
{
	uint idxOut[];
};

layout(std430, binding=4) restrict writeonly buffer buf_statesOut
{
	uint8_t statesOut[];
};

layout(std430, binding=5) restrict writeonly buffer buf_risingOut
{
	uint8_t risingOut[];
};

layout(std430, binding=6) restrict writeonly buffer buf_finalCount
{
	uint finalCount;
};

layout(std430, push_constant) uniform constants
{
	uint len;
	uint order;
	uint inputPerThread;
	uint outputPerThread;
};

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

/**
	@brief Second-pass zero crossing detection

	Merge the output from the first-pass shader
 */
void main()
{
	//Find our block of inputs
	uint numThreads = gl_NumWorkGroups.x * gl_WorkGroupSize.x;

	//Find starting sample index
	uint writebase = 0;
	for(uint i=0; i<gl_GlobalInvocationID.x; i++)
		writebase += idxIn[i * outputPerThread];

	//Find number of samples to copy
	uint readbase = gl_GlobalInvocationID.x * outputPerThread;
	uint numSamples = idxIn[readbase];
	readbase ++;	//skip the size value at position 0

	//Actually do the copy
	for(uint i=0; i<numSamples; i++)
	{
		idxOut[writebase + i] = idxIn[readbase + i];
		statesOut[writebase + i] = statesIn[readbase + i];
		risingOut[writebase + i] = risingIn[readbase + i];
	}

	if(gl_GlobalInvocationID.x == (numThreads - 1) )
		finalCount = writebase + numSamples;
}
