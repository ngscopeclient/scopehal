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

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

/**
	@brief First-pass zero crossing detection

	Each thread independently processes a block of inputPerThread samples and outputs a variable-length block, from
	0 to outputPerThread-1 in size, of samples
 */
void main()
{
	//Find our block of inputs
	uint numThreads = gl_NumWorkGroups.x * gl_WorkGroupSize.x;
	uint instart = gl_GlobalInvocationID.x * inputPerThread;
	uint inend = instart + inputPerThread;
	if(gl_GlobalInvocationID.x == (numThreads - 1))
		inend = len;

	//Block of outputs
	uint nouts = 0;
	uint outbase = gl_GlobalInvocationID.x * outputPerThread;

	if(instart == 0)
		instart ++;

	uint numThresholds = order-1;
	uint maxOuts = outputPerThread - 1;

	for(uint i = instart; (i < inend) && (nouts < maxOuts); i ++)
	{
		//Check against each threshold for both rising and falling edges
		float prev = samples[i-1];
		float cur = samples[i];

		//Prepare to make a new edge
		for(uint j=0; j<numThresholds; j++)
		{
			float t = thresholds[j];
			uint iout = outbase + nouts + 1;

			//Check for rising edge
			if( (prev <= t) && (cur > t) )
			{
				idx[iout] = i;
				rising[iout] = uint8_t(1);
				states[iout] = uint8_t(j+1);
				nouts ++;
				break;
			}

			//Check for falling edge
			else if( (prev >= t) && (cur < t) )
			{
				idx[iout] = i;
				rising[iout] = uint8_t(0);
				states[iout] = uint8_t(j);
				nouts ++;
				break;
			}
			//else not a level crossing
		}
	}

	//Save number of outputs we found
	idx[outbase] = nouts;
}
