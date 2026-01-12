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
	uint dout[];
};

layout(std430, push_constant) uniform constants
{
	uint len;
};

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

void main()
{
	//Search interleaved for more efficient memory accesses
	uint numThreads = gl_NumWorkGroups.x * gl_WorkGroupSize.x;
	uint end = len - 11;
	bool found = false;
	for(uint i=gl_GlobalInvocationID.x; i < end; i += numThreads)
	{
		bool hit = true;
		if(uint(din[i + 0]) != 1)
			hit = false;
		if(uint(din[i + 1]) != 1)
			hit = false;
		if(uint(din[i + 2]) != 0)
			hit = false;
		if(uint(din[i + 3]) != 0)
			hit = false;
		if(uint(din[i + 4]) != 0)
			hit = false;
		if(uint(din[i + 5]) != 1)
			hit = false;
		if(uint(din[i + 6]) != 0)
			hit = false;
		if(uint(din[i + 7]) != 0)
			hit = false;
		if(uint(din[i + 8]) != 0)
			hit = false;
		if(uint(din[i + 9]) != 1)
			hit = false;

		if(hit)
		{
			dout[gl_GlobalInvocationID.x] = i;
			found = true;
			break;
		}
	}

	//if we found nothing, report error
	if(!found)
		dout[gl_GlobalInvocationID.x] = 0xffffffff;
}
