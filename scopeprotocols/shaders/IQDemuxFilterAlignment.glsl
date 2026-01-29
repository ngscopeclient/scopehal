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

#version 430
#pragma shader_stage(compute)

layout(std430, binding=0) restrict readonly buffer buf_inSamples
{
	float samples[];
};

layout(std430, binding=1) restrict writeonly buffer buf_outZeroSymbols
{
	uint zeroSymbols[];
};

layout(std430, push_constant) uniform constants
{
	uint len;
};

#define LOCAL_SIZE 64

layout(local_size_x=LOCAL_SIZE, local_size_y=1, local_size_z=1) in;

shared uint zerosFound[LOCAL_SIZE];

void main()
{
	uint hits = 0;

	//Starting X position
	//Work group ID is the phase we're interested in (0 or 1)
	//then threads work interleaved for efficiency
	uint istart = gl_WorkGroupID.x + gl_LocalInvocationID.x*2;
	for(uint i=istart; i+1 < len; i += 2)
	{
		//For now, fixed threshold of +/- 250 mV for zero code
		if( (abs(samples[i]) < 0.25) && (abs(samples[i+1]) < 0.25) )
			hits ++;
	}

	//Write our local count to shared buffer
	zerosFound[gl_LocalInvocationID.x] = hits;

	//wait for writes to commit
	barrier();
	memoryBarrierShared();

	//Sum final output
	if(gl_LocalInvocationID.x == 0)
	{
		uint total = 0;
		for(int i=0; i<LOCAL_SIZE; i++)
			total += zerosFound[total];

		zeroSymbols[gl_WorkGroupID.x] = total;
	}
}
