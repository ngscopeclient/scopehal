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

layout(std430, binding=0) restrict writeonly buffer buf_dout
{
	float dout[];
};

layout(std430, push_constant) uniform constants
{
	uint fpfreq;
	uint numSamples;
	uint samplesPerThread;
	uint rngSeed;
	float startPhase;
	float scale;
	float sigma;
};

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

void main()
{
	//Base thread ID
	uint nthread = (gl_GlobalInvocationID.y * gl_NumWorkGroups.x * gl_WorkGroupSize.x) + gl_GlobalInvocationID.x;

	//Bounds for our generation
	uint istart = nthread * samplesPerThread;
	uint iend = istart + samplesPerThread;
	if(iend >= numSamples)
		iend = numSamples - 1;

	float two_pi = 6.28318530717;

	//Create the output
	uint state = rngSeed + nthread*13;
	for(uint i=istart; i <= iend; i += 2)
	{
		//Generate two pseudorandom uint32's with the first being nonzero, using xorshift32
		uint rngOut[2] = {0, 0};
		uint rngmax = 0xffffff;
		for(uint j=0; j<2; j++)
		{
			while(rngOut[j] == 0)
			{
				uint x = state;
				x ^= (x << 13);
				x ^= (x >> 17);
				x ^= (x << 5);
				state = x;

				rngOut[j] = x & rngmax;
			}
		}

		//Convert the random ints to floats in [0, 1]
		float u1 = float(rngOut[0]) / float(rngmax);
		float u2 = float(rngOut[1]) / float(rngmax);

		//Convert to uniform distribution using Box-Muller
		float mag = sigma * sqrt(-2 * log(u1));
		float noise0 = mag * cos(two_pi * u2);
		float noise1 = mag * sin(two_pi * u2);

		//Get the fractional phase using integer math with implicit mod 2^32 to handle wrapping
		const float fix_to_float = 1.0 / 4294967295.0;
		uint fpfrac0 = i * fpfreq;
		uint fpfrac1 = (i + 1) * fpfreq;
		float frac0 = float(fpfrac0) * fix_to_float;
		float frac1 = float(fpfrac1) * fix_to_float;

		//Generate the output (second sample needs separate bounds check)
		dout[i] = scale * sin(frac0*two_pi + startPhase) + noise0;
		if(i+1 <= iend)
			dout[i+1] = scale * sin(frac1*two_pi + startPhase) + noise1;
	}
}
