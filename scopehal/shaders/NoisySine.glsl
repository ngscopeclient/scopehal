/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2025 Andrew D. Zonenberg and contributors                                                         *
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
	uint numSamples;
	uint samplesPerThread;
	uint rngSeed;
	float startPhase;
	float scale;
	float sigma;
	float radiansPerSample;
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

	const float twopi = 2 * 3.1415926535;

	//Create the output
	uint lcgState = rngSeed + nthread;
	for(uint i=istart; i <= iend; i += 2)
	{
		//Generate two pseudorandom uint32's with the first being nonzero
		//Use glibc rand() parameters
		uint rngOut[2] = {0, 0};
		uint rngmax = 0xffffff;
		for(uint j=0; j<2; j++)
		{
			while(rngOut[j] == 0)
			{
				lcgState = ( (lcgState * 1103515245) + 12345 ) & 0x7fffffff;
				rngOut[j] = lcgState & rngmax;

				if(j == 1)
					break;
			}
		}

		//Convert the random ints to floats in [0, 1]
		float u1 = float(rngOut[0]) / float(rngmax);
		float u2 = float(rngOut[1]) / float(rngmax);

		//Convert to uniform distribution using Box-Muller
		float mag = sigma * sqrt(-2 * log(u1));
		float noise0 = mag * cos(twopi * u2);
		float noise1 = mag * sin(twopi * u2);

		//Generate the output (second sample needs separate bounds check)
		dout[i] = scale * sin(i * radiansPerSample + startPhase) + noise0;
		if(i+1 <= iend)
			dout[i+1] = scale * sin((i+1) * radiansPerSample + startPhase) + noise1;
	}
}
