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

#extension GL_ARB_gpu_shader_int64 : require

layout(std430, binding=0) restrict readonly buffer buf_inSamples
{
	float samplesIn[];
};

layout(std430, binding=1) restrict writeonly buffer buf_outOffsets
{
	int64_t offsetsOut[];
};

layout(std430, binding=2) restrict writeonly buffer buf_outSamples
{
	float samplesOut[];
};

layout(std430, push_constant) uniform constants
{
	int64_t timescale;
	uint	len;
	uint	bufferPerThread;
	float	vstart;
	float	vend;
};

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

#include "../../scopehal/shaders/InterpolateTime.h.glsl"

void main()
{
	//Initial starting sample indexes for this thread
	uint numThreads = gl_NumWorkGroups.x * gl_WorkGroupSize.x;
	uint numSamplesPerThread = len / numThreads;
	uint startpos = gl_GlobalInvocationID.x * numSamplesPerThread;
	uint outbase = bufferPerThread * gl_GlobalInvocationID.x;

	//Adjust starting position to the left until we find a rising edge,
	//the start of the waveform, or the start of a previous block
	if(gl_GlobalInvocationID.x != 0)
	{
		uint minpos = startpos - numSamplesPerThread;

		for(; startpos > minpos; startpos --)
		{
			if(samplesIn[startpos] > vstart)
				break;
		}

		//If we hit the edge of the previous block, we had no edges at all for this thread.
		//Stop and do nothing.
		if(startpos == minpos)
		{
			offsetsOut[outbase] = 0;
			return;
		}
	}

	//Ending position
	uint endpos;
	if( (gl_GlobalInvocationID.x + 1) == numThreads)
		endpos = len;
	else
		endpos = (gl_GlobalInvocationID.x+1) * numSamplesPerThread;

	//Main loop
	int state = 0;
	float last = -1e20;
	uint numOutputSamples = 0;
	int64_t tedge = 0;
	for(uint i=startpos; i < endpos; i++)
	{
		float cur = samplesIn[i];
		int64_t tnow = int64_t(i) * timescale;

		//Find start of edge
		if(state == 0)
		{
			if( (cur < vstart) && (last >= vstart) )
			{
				float xdelta = InterpolateTime(last, cur, vstart) * float(timescale);
				tedge = tnow - timescale + int64_t(xdelta);
				state = 1;
			}
		}

		//Find end of edge
		else if(state == 1)
		{
			if( (cur < vend) && (last >= vend) )
			{
				float xdelta = InterpolateTime(last, cur, vend) * float(timescale);
				int64_t dt = int64_t(xdelta) + tnow - timescale - tedge;

				uint iout = outbase + numOutputSamples + 1;
				samplesOut[iout] = float(dt);
				offsetsOut[iout] = tedge;
				numOutputSamples ++;
			}
		}

		last = cur;
	}

	//Save number of samples at the start of the output buffer
	offsetsOut[outbase] = int64_t(numOutputSamples);
}
