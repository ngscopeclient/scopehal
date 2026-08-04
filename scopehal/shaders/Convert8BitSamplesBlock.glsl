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

	This shader is a drop-in replacement for Convert8BitSamples except requires less threads to be launched,
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

#define SAMPLES_PER_WORD 	4
#define WORDS_PER_THREAD	4
#define THREADS_PER_BLOCK	64

layout(local_size_x=THREADS_PER_BLOCK, local_size_y=1, local_size_z=1) in;

void main()
{
	uint nthread = (gl_GlobalInvocationID.y * gl_NumWorkGroups.x * gl_WorkGroupSize.x) + gl_GlobalInvocationID.x;
	uint inbase = nthread * WORDS_PER_THREAD;
	uint outbase = nthread * SAMPLES_PER_WORD * WORDS_PER_THREAD;

	//Don't go off the end of the input
	if(outbase >= size)
		return;

	//Fetch the input samples, in a larger block.
	//(We're PCIe bound and want to allow other stuff to coexist with us etc)
	uint block[WORDS_PER_THREAD];
	for(uint nword=0; (nword<WORDS_PER_THREAD) && ( (outbase + 4*nword) < size); nword++)
		block[nword] = pin[inbase + nword];

	//Crunch them
	for(uint nword = 0; nword < WORDS_PER_THREAD; nword ++)
	{
		uint wblock = block[nword];
		uint ioutbase = nword*SAMPLES_PER_WORD + outbase;

		for(uint i=0; i<SAMPLES_PER_WORD; i++)
		{
			//Make sure we don't go off the end
			uint j = i + ioutbase;
			if(j >= size)
				return;

			//Fetch the sample and sign extend
			uint sampleIn = (wblock >> (8*i)) & 0xff;
			int signExtended = int(sampleIn);
			if( (sampleIn & 0x80) == 0x80)
			{
				sampleIn = (~sampleIn + 1) & 0xff;
				signExtended = -int(sampleIn);
			}

			//Do the actual conversion
			pout[j] = gain * float(int(signExtended)) - offset;
		}
	}
}
