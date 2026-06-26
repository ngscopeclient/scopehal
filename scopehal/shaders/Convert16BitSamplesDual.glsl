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

#version 430
#pragma shader_stage(compute)

/**
	@brief Signed int8 to float32 conversion

	This shader is a drop-in replacement for Convert8BitSamples except requires 1/4 as many threads to be launched,
	and does not need GL_EXT_shader_8bit_storage
 */

layout(std430, binding=0) restrict writeonly buffer buf_pout
{
	float pout[];
};

layout(std430, binding=1) restrict readonly buffer buf_pin
{
	uint pin[];
};

layout(std430, push_constant) uniform constants
{
	uint size;
	float gain;
	float offset;
};

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

void main()
{
	uint nthread = (gl_GlobalInvocationID.y * gl_NumWorkGroups.x * gl_WorkGroupSize.x) + gl_GlobalInvocationID.x;
	uint base = nthread * 2;

	//Don't go off the end of the input
	if(base >= size)
		return;

	//Fetch the input sample
	uint block = pin[nthread];

	//Four samples per thread
	for(uint i=0; i<2; i++)
	{
		//Make sure we don't go off the end
		uint j = i+base;
		if(j >= size)
			return;

		//Fetch the sample and sign extend
		uint sampleIn = (block >> (16*i)) & 0xffff;
		int signExtended = int(sampleIn);
		if( (sampleIn & 0x8000) == 0x8000)
		{
			sampleIn = (~sampleIn + 1) & 0xffff;
			signExtended = -int(sampleIn);
		}

		//Do the actual conversion
		pout[j] = gain * float(int(signExtended)) - offset;
	}
}
