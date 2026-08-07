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

#extension GL_EXT_shader_8bit_storage : require

layout(std430, binding=0) restrict readonly buffer buf_pin
{
	float samples[];
};

layout(std430, binding=1) restrict readonly buffer buf_thresholds
{
	float thresholds[];
};

layout(std430, binding=2) restrict writeonly buffer buf_indexes
{
	uint idx[];
};

layout(std430, binding=3) restrict writeonly buffer buf_states
{
	uint8_t states[];
};

layout(std430, binding=4) restrict writeonly buffer buf_rising
{
	uint8_t rising[];
};

layout(std430, push_constant) uniform constants
{
	uint len;
	uint order;
	uint inputPerThread;
	uint outputPerThread;
};

#define X_SIZE 64

shared uint s_rising[X_SIZE];
shared uint s_states[X_SIZE];
shared bool s_hit[X_SIZE];

layout(local_size_x=X_SIZE, local_size_y=1, local_size_z=1) in;

/**
	@brief First-pass zero crossing detection

	Each thread independently processes a block of inputPerThread samples and outputs a variable-length block, from
	0 to outputPerThread-1 in size, of samples
 */
void main()
{
	//Find our block of inputs
	uint numThreads = gl_NumWorkGroups.y * gl_WorkGroupSize.y;
	uint instart = gl_GlobalInvocationID.y * inputPerThread;
	uint inend = instart + inputPerThread;

	//Block of outputs
	uint nouts = 0;

	//Need a previous sample so make the first block start at n=1 not n=0
	if(instart == 0)
		instart ++;

	//Clamp end
	if(inend >= len)
		inend = len;

	//If we start after the end of the array, stop
	if(instart >= len)
	{
		if(gl_LocalInvocationID.x == 0)
			idx[gl_GlobalInvocationID.y] = 0;
		return;
	}

	uint numThresholds = order-1;
	uint maxOuts = outputPerThread - 1;

	for(uint i = instart; (i < inend) && (nouts < maxOuts); i += X_SIZE)
	{
		//Need to bounds check within the loop since we can run up to X_SIZE off the end in the last iteration
		uint ireal = i + gl_LocalInvocationID.x;
		s_hit[gl_LocalInvocationID.x] = false;
		if(ireal < inend)
		{
			//Check against each threshold for both rising and falling edges
			float prev = samples[ireal-1];
			float cur = samples[ireal];
			for(uint j=0; j<numThresholds; j++)
			{
				float t = thresholds[j];

				//Check for rising edge
				if( (prev <= t) && (cur > t) )
				{
					s_hit[gl_LocalInvocationID.x] = true;
					s_rising[gl_LocalInvocationID.x] = 1;
					s_states[gl_LocalInvocationID.x] = j+1;
					break;
				}

				//Check for falling edge
				else if( (prev >= t) && (cur < t) )
				{
					s_hit[gl_LocalInvocationID.x] = true;
					s_rising[gl_LocalInvocationID.x] = 0;
					s_states[gl_LocalInvocationID.x] = j;
					break;
				}
			}
		}

		//Sync and write back
		barrier();
		for(uint j=0; j<gl_WorkGroupSize.x; j++)
		{
			if(s_hit[j])
			{
				if(j == gl_LocalInvocationID.x)
				{
					uint iout = (nouts + 1) * numThreads + gl_GlobalInvocationID.y;

					idx[iout] = i + j;

					//TODO: can we merge these into less 32-bit writes?
					rising[iout] = uint8_t(s_rising[j]);
					states[iout] = uint8_t(s_states[j]);
				}

				nouts ++;

				if(nouts >= maxOuts)
					break;
			}
		}
		barrier();
	}

	//Save number of outputs we found
	if(gl_LocalInvocationID.x == 0)
		idx[gl_GlobalInvocationID.y] = nouts;
}
