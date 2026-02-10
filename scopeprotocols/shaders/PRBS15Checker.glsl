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
#extension GL_EXT_shader_8bit_storage : require

layout(std430, binding=0) restrict readonly buffer buf_din
{
	uint8_t din[];
};

layout(std430, binding=1) restrict writeonly buffer buf_dout
{
	uint8_t dout[];
};

layout(std430, push_constant) uniform constants
{
	uint count;
};

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

void main()
{
	//Range calculation
	const uint PRBS_LEN = 32767;
	const uint PRBS_BITS = 15;
	uint nthread = (gl_GlobalInvocationID.y * gl_NumWorkGroups.x * gl_WorkGroupSize.x) + gl_GlobalInvocationID.x;
	uint startpos = (nthread * PRBS_LEN);
	uint endpos = startpos + PRBS_LEN;

	//Clamp loop bounds to requested dataset size
	if(startpos > count)
		return;
	if(endpos > count)
		endpos = count;

	//Use the first few PRBS bits as the starting state
	uint state = 0;
	for(uint i=0; i<PRBS_BITS; i++)
	{
		state = (state << 1);
		if(uint(din[i]) != 0)
			state = state | 1;
	}

	//Verify the LFSR state bits (this will always return correct for the first block since it was the seed)
	for(uint i=0; i<PRBS_BITS; i++)
	{
		if(uint(din[i]) == uint(din[startpos + i]))
			dout[startpos+i] = uint8_t(0);
		else
			dout[startpos+i] = uint8_t(1);
	}
	startpos += PRBS_BITS;

	//PRBS verification
	for(uint i=startpos; i<endpos; i++)
	{
		uint next = ( (state >> 14) ^ (state >> 13) ) & 1;
		state = (state << 1) | next;

		if(next == uint(din[i]))
			dout[i] = uint8_t(0);
		else
			dout[i] = uint8_t(1);
	}
}
