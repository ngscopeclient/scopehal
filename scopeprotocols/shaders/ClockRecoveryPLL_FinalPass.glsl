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

layout(std430, binding=0) restrict readonly buffer buf_offsetsFirstPass
{
	int64_t offsetsFirstPass[];
};

layout(std430, binding=1) restrict readonly buffer buf_stateFirstPass
{
	int64_t stateFirstPass[];
};

layout(std430, binding=2) restrict readonly buffer buf_offsetsSecondPass
{
	int64_t offsetsSecondPass[];
};

layout(std430, binding=3) restrict readonly buffer buf_stateSecondPass
{
	int64_t stateSecondPass[];
};

layout(std430, binding=4) restrict writeonly buffer buf_offsets
{
	int64_t offsets[];
};

layout(std430, push_constant) uniform constants
{
	int64_t	initialPeriod;
	int64_t fnyquist;
	int64_t	tend;
	uint	nedges;
	uint	maxOffsetsPerThread;
};

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

void main()
{
	//If this is the first thread, special case since we copy from the first block
	if(gl_GlobalInvocationID.x == 0)
	{
		uint count = uint(stateFirstPass[0]);
		int64_t lastOffset = offsetsFirstPass[0];
		for(uint i=1; i<count; i++)
		{
			int64_t nextOffset = offsetsFirstPass[i];
			int64_t delta = nextOffset - lastOffset;
			offsets[i-1] = lastOffset + delta/2;	//90 degree phase shift
			lastOffset = nextOffset;
		}

		//We don't have a next sample to compare to, so phase shift by the 90 degrees WRT the final NCO phase
		offsets[count-1] = lastOffset + stateFirstPass[1]/2;
	}

	//Everything else copies from subsequent blocks
	else
	{
		//Find starting sample index
		uint writebase = uint(stateFirstPass[0]);
		for(uint i=1; i<gl_GlobalInvocationID.x; i++)
			writebase += uint(stateSecondPass[(i-1)*2]);

		//Copy samples
		uint count = uint(stateSecondPass[(gl_GlobalInvocationID.x - 1)*2]);
		uint readbase = (gl_GlobalInvocationID.x - 1) * maxOffsetsPerThread;
		int64_t lastOffset = offsetsSecondPass[readbase];
		for(uint i=1; i<count; i++)
		{
			int64_t nextOffset = offsetsSecondPass[readbase + i];
			int64_t delta = nextOffset - lastOffset;
			offsets[writebase + i - 1] = lastOffset + delta/2;	//90 degree phase shift
			lastOffset = nextOffset;
		}

		//We don't have a next sample to compare to, so phase shift by the 90 degrees WRT the final NCO phase
		offsets[writebase + count - 1] = lastOffset + stateSecondPass[(gl_GlobalInvocationID.x - 1)*2 + 1]/2;
	}
}
