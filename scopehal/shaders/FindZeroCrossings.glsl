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

#version 460
#pragma shader_stage(compute)
#extension GL_ARB_gpu_shader_int64 : require

layout(std430, binding=0) restrict writeonly buffer buf_pout
{
	int64_t pout[];
};

layout(std430, binding=1) restrict readonly buffer buf_pin
{
	float pin[];
};

layout(std430, push_constant) uniform constants
{
	int64_t triggerPhase;	//Trigger timestamp offset for the input
	int64_t timescale;		//Input waveform timebase units per tick
	uint inputSize;			//Total number of input samples
	uint inputPerThread;	//Number of input samples handled by one thread
	uint outputPerThread;	//Number of output samples handled by one thread
							//(must be 1+inputPerThread to allow for the size field in the first slot)
	float threshold;
	float ftimescale;		//Input waveform timebase units per tick, rounded to float
};

#define X_SIZE 64

layout(local_size_x=X_SIZE, local_size_y=1, local_size_z=1) in;

#include "InterpolateTime.h.glsl"

shared bool g_hit[X_SIZE];
shared float g_tfrac[X_SIZE];

/**
	@brief First-pass zero crossing detection

	Each thread independently processes a block of inputPerThread samples and outputs a variable-length block, from
	0 to outputPerThread-1 in size, of samples
 */
void main()
{
	//Find our block of inputs
	uint nthread = (gl_GlobalInvocationID.z * gl_NumWorkGroups.y * gl_WorkGroupSize.y) + gl_GlobalInvocationID.y;
	uint instart = nthread * inputPerThread;
	uint inend = instart + inputPerThread;
	if(inend > inputSize)
		inend = inputSize;

	//Find our block of outputs
	int nouts = 0;
	uint outstart = nthread * outputPerThread;
	uint iout = outstart + 1;

	//Search for level crossings within our block
	for(uint i=instart; i<inend; i += X_SIZE)
	{
		//Get actual sample index
		uint ireal = i + gl_LocalInvocationID.x;

		//Bounds check: need to be not the first sample, and not off the end
		if( (ireal > 0) && (ireal < inend) )
		{
			float fa = pin[ireal - 1];
			float fb = pin[ireal];

			bool prevValue = fa > threshold;
			bool currentValue = fb > threshold;

			if(currentValue != prevValue)
			{
				g_hit[gl_LocalInvocationID.x] = true;
				g_tfrac[gl_LocalInvocationID.x] = ftimescale * InterpolateTime(fa, fb, threshold);
			}
			else
				g_hit[gl_LocalInvocationID.x] = false;
		}
		else
			g_hit[gl_LocalInvocationID.x] = false;

		barrier();

		//Write back results
		if(gl_LocalInvocationID.x == 0)
		{
			for(uint j=0; j<X_SIZE; j++)
			{
				if(g_hit[j])
				{
					pout[iout] = triggerPhase + timescale*int64_t(j + i - 1) + int64_t(g_tfrac[j]);
					iout ++;
					nouts ++;
				}
			}
		}

		barrier();
	}

	//Save number of outputs we found at the first word in the block
	if(gl_LocalInvocationID.x == 0)
		pout[outstart] = nouts;
}
