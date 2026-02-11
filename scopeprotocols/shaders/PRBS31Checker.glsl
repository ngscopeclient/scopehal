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

layout(std430, binding=2) restrict readonly buffer buf_lfsrTable
{
	uint lfsrTable[];
};

layout(std430, push_constant) uniform constants
{
	uint count;
	uint samplesPerThread;
};

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

void main()
{
	const uint PRBS_BITS = 31;

	//Range calculation
	uint nthread = (gl_GlobalInvocationID.y * gl_NumWorkGroups.x * gl_WorkGroupSize.x) + gl_GlobalInvocationID.x;
	uint startpos = nthread * samplesPerThread;

	//Figure out starting LFSR state given seed and starting position
	//Use the first few PRBS bits as the starting state
	uint state = 0;
	for(uint i=0; i<PRBS_BITS; i++)
	{
		state = (state << 1);
		if(uint(din[i]) != 0)
			state = state | 1;
	}

	//Skip first N samples that we used as initial state
	uint endpos = startpos + samplesPerThread;
	if(nthread == 0)
		startpos = PRBS_BITS;

	//Seek into the LFSR data stream to find our starting position
	else
	{
		//Back up by one LFSR size to account for the starting state bits
		//(no modulus because LFSR repetition length is longer than our max record length)
		uint startposMod = (startpos - PRBS_BITS);

		//Calculate LFSR state at the start of this block given the initial state
		//only 30 rows because max record length is 2^30
		uint tmp = 0;
		for(uint iterbit = 0; iterbit < 30; iterbit ++)
		{
			//if input bit is set, use that table entry
			if( (startposMod & (1 << iterbit)) != 0 )
			{
				tmp = 0;

				//xor each table entry into it
				for(int i=0; i<PRBS_BITS; i++)
				{
					if( (state & (1 << i) ) != 0)
						tmp ^= lfsrTable[iterbit*PRBS_BITS + i];
				}

				state = tmp;
			}
		}
	}

	//Clamp to iteration bounds
	if(startpos > count)
		return;
	if(endpos > count)
		endpos = count;

	//Zero out error flags for beginning of the output
	if(nthread == 0)
	{
		for(uint i=0; i<PRBS_BITS; i++)
			dout[i] = uint8_t(0);
	}

	//PRBS verification
	for(uint i=startpos; i<endpos; i++)
	{
		uint next = ( (state >> 30) ^ (state >> 27) ) & 1;
		state = (state << 1) | next;

		if(next == uint(din[i]))
			dout[i] = uint8_t(0);
		else
			dout[i] = uint8_t(1);
	}

}
