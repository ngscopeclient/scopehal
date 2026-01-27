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
#extension GL_ARB_gpu_shader_int64 : require

layout(std430, binding=0) restrict readonly buffer buf_indexes
{
	uint indexes[];
};

layout(std430, binding=1) restrict readonly buffer buf_states
{
	uint8_t states[];
};

layout(std430, binding=2) restrict readonly buffer buf_rising
{
	uint8_t rising[];
};

layout(std430, binding=3) restrict readonly buffer buf_samplesIn
{
	float samples[];
};

layout(std430, binding=4) restrict readonly buffer buf_levelsIn
{
	float levels[];
};

layout(std430, binding=5) restrict writeonly buffer buf_offsetsOut
{
	int64_t offsets[];
};

layout(std430, push_constant) uniform constants
{
	int64_t	halfui;
	int64_t timescale;
	uint numIndexes;
	uint inputPerThread;
	uint outputPerThread;
	uint order;
};

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

#include "../../scopehal/shaders/InterpolateTime.h.glsl"

/**
	@brief Second-pass zero crossing detection

	Merge the output from the first-pass shader
 */
void main()
{
	//Find our block of inputs
	uint numThreads = gl_NumWorkGroups.x * gl_WorkGroupSize.x;
	uint instart = gl_GlobalInvocationID.x * inputPerThread;
	uint inend = instart + inputPerThread;
	if(inend > numIndexes)
		inend = numIndexes;

	//Block of outputs
	uint outbase = gl_GlobalInvocationID.x * outputPerThread;

	///Skip the first sample of the waveform if we have an edge there, since we can't interpolate
	if(gl_GlobalInvocationID.x == 0)
	{
		if(indexes[0] == 0)
			instart ++;
	}

	//Loop over edges and determine which groups of edges are actually a single level crossing
	uint nouts = 0;
	for(uint i=instart; i < inend; i++)
	{
		uint istart = indexes[i] - 1;
		uint iend = indexes[i] + 1;
		uint symstart;
		uint symend = uint(states[i]);

		uint isRising = uint(rising[i]);
		if(isRising != 0)
			symstart = symend - 1;
		else
			symstart = symend + 1;

		//If the previous edge is close to this one (< 0.5 UI)
		//and they're both rising or falling, merge them
		bool merging = false;
		for(uint lookback = 1; lookback < order-1; lookback ++)
		{
			if(i <= lookback)
				break;

			int64_t delta = int64_t(indexes[i] - indexes[i-lookback]) * timescale;
			if( (uint(rising[i-lookback]) == isRising) && (delta < halfui) )
			{
				merging = true;
				istart = indexes[i-lookback]-1;

				if(isRising != 0)
					symstart = symend - (lookback+1);
				else
					symstart = symend + (lookback+1);
			}
			else
				break;
		}

		//Find the midpoint (for now, fixed threshold still)
		float target = (levels[symstart] + levels[symend]) / 2;
		int64_t tlerp = 0;
		for(uint j=istart; j<iend; j++)
		{
			float prev = samples[j-1];
			float cur = samples[j];

			if(	( (prev <= target) && (cur > target) ) ||
				( (prev >= target) && (cur < target) ) )
			{
				float delta = InterpolateTime(samples[j-1], samples[j], target) * float(timescale);
				tlerp = int64_t(j-1)*timescale + int64_t(delta);
				break;
			}
		}

		//Create a new edge
		if(!merging)
		{
			offsets[outbase + nouts + 1] = tlerp;
			nouts ++;
		}

		//Overwrite previous sample with new edge timestamp
		else
			offsets[outbase + nouts] = tlerp;
	}

	//Write final output count
	offsets[outbase] = int64_t(nouts);
}
