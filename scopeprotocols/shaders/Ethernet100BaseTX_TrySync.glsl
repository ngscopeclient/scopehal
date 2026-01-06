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
	uint len;
};

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

void main()
{
	//Bounds check
	const uint searchWindow = 64;
	if( (gl_GlobalInvocationID.x + searchWindow) >= len)
	{
		dout[gl_GlobalInvocationID.x] = uint8_t(0);
		return;
	}

	//Assume the link is idle at this offset, then see if we got it right
	//TODO: can we make this bit twiddling a bit less verbose??
	uint lfsr = 0;
	if(uint(din[gl_GlobalInvocationID.x + 0]) == 0)
		lfsr |= (1 << 10);
	if(uint(din[gl_GlobalInvocationID.x + 1]) == 0)
		lfsr |= (1 << 9);
	if(uint(din[gl_GlobalInvocationID.x + 2]) == 0)
		lfsr |= (1 << 8);
	if(uint(din[gl_GlobalInvocationID.x + 3]) == 0)
		lfsr |= (1 << 7);
	if(uint(din[gl_GlobalInvocationID.x + 4]) == 0)
		lfsr |= (1 << 6);
	if(uint(din[gl_GlobalInvocationID.x + 5]) == 0)
		lfsr |= (1 << 5);
	if(uint(din[gl_GlobalInvocationID.x + 6]) == 0)
		lfsr |= (1 << 4);
	if(uint(din[gl_GlobalInvocationID.x + 7]) == 0)
		lfsr |= (1 << 3);
	if(uint(din[gl_GlobalInvocationID.x + 8]) == 0)
		lfsr |= (1 << 2);
	if(uint(din[gl_GlobalInvocationID.x + 9]) == 0)
		lfsr |= (1 << 1);
	if(uint(din[gl_GlobalInvocationID.x + 10]) == 0)
		lfsr |= (1 << 0);

	//We should have at least 64 "1" bits in a row once the descrambling is done.
	//The minimum inter-frame gap is a lot bigger than this.
	uint start = gl_GlobalInvocationID.x + 11;
	uint stop = start + searchWindow;
	bool ok = true;
	for(uint i=start; i < stop; i++)
	{
		uint c = ( (lfsr >> 8) ^ (lfsr >> 10) ) & 1;
		lfsr = (lfsr << 1) ^ c;

		//If it's not a 1 bit (idle character is all 1s), no go
		if( (uint(din[i]) ^ c) != 1)
		{
			ok = false;
			break;
		}
	}

	if(ok)
		dout[gl_GlobalInvocationID.x] = uint8_t(1);
	else
		dout[gl_GlobalInvocationID.x] = uint8_t(0);
}
