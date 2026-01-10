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
#extension GL_EXT_shader_8bit_storage : require

layout(std430, binding=0) restrict readonly buffer buf_offsetsFirstPass
{
	int64_t offsetsFirstPass[];
};

layout(std430, binding=1) restrict readonly buffer buf_stateFirstPass
{
	int64_t stateFirstPass[];
};

layout(std430, binding=2) restrict readonly buffer buf_offsetsSecondPass
{
	int64_t offsetsSecondPass[];
};

layout(std430, binding=3) restrict readonly buffer buf_stateSecondPass
{
	int64_t stateSecondPass[];
};

layout(std430, binding=4) restrict writeonly buffer buf_offsets
{
	int64_t offsets[];
};

layout(std430, binding=5) restrict writeonly buffer buf_squarewave
{
	uint8_t squarewave[];
};

layout(std430, binding=6) restrict writeonly buffer buf_durations
{
	int64_t durations[];
};

layout(std430, binding=7) restrict writeonly buffer buf_ssamples
{
	float ssamples[];
};

layout(std430, binding=8) restrict readonly buffer buf_din
{
	float isamples[];
};

layout(std430, push_constant) uniform constants
{
	int64_t	initialPeriod;
	int64_t fnyquist;
	int64_t	tend;
	int64_t	timescale;
	int64_t	triggerPhase;
	uint	nedges;
	uint	maxOffsetsPerThread;
	uint	maxInputSamples;
};

#define X_SIZE 8
#define Y_SIZE 64

layout(local_size_x=X_SIZE, local_size_y=Y_SIZE, local_size_z=1) in;

shared int64_t prefetchBlock[Y_SIZE][X_SIZE + 1];

void main()
{
	//If this is the first thread, special case since we copy from the first block
	if(gl_GlobalInvocationID.y == 0)
	{
		uint count = uint(stateFirstPass[0]);

		//Copy samples
		for(uint i=1; i<count; i += X_SIZE)
		{
			//Prefetch
			prefetchBlock[gl_LocalInvocationID.y][gl_LocalInvocationID.x + 1] =
				offsetsFirstPass[i + gl_LocalInvocationID.x];
			barrier();
			memoryBarrierShared();

			if(gl_GlobalInvocationID.x != 0)
				continue;

			//Load the 0th entry
			prefetchBlock[gl_LocalInvocationID.y][0] = offsetsFirstPass[i - 1];

			for(uint j=0; j<X_SIZE && (i+j) < count; j++)
			{
				uint iout = i + j - 1;
				int64_t lastOffset = prefetchBlock[gl_LocalInvocationID.y][j];
				int64_t nextOffset = prefetchBlock[gl_LocalInvocationID.y][j+1];
				squarewave[iout] = uint8_t(iout & 1);

				//Generate the squarewave output
				uint nsample = min(uint((lastOffset - triggerPhase) / timescale), maxInputSamples-1);
				float sampledData = isamples[nsample];
				offsets[iout] = lastOffset;
				durations[iout] = nextOffset - lastOffset;

				//Generate sampled data output
				ssamples[iout] = sampledData;
			}
		}

		if(gl_GlobalInvocationID.x != 0)
			return;

		//Last sample
		int64_t lastOffset = offsetsFirstPass[count-1];
		uint iout = count - 1;
		int64_t lastPeriod = stateFirstPass[1];
		int64_t tout = lastOffset;
		offsets[iout] = tout;
		squarewave[iout] = uint8_t(iout & 1);
		durations[iout] = lastPeriod;

		//Generate sampled data output
		uint nsample = min(uint((tout - triggerPhase) / timescale), maxInputSamples-1);
		ssamples[iout] = isamples[nsample];
	}

	//Everything else copies from subsequent blocks
	else
	{
		//Find starting sample index
		uint writebase = uint(stateFirstPass[0]);
		for(uint i=1; i<gl_GlobalInvocationID.y; i++)
			writebase += uint(stateSecondPass[(i-1)*3]);

		uint count = uint(stateSecondPass[(gl_GlobalInvocationID.y - 1)*3]);
		uint readbase = (gl_GlobalInvocationID.y - 1) * maxOffsetsPerThread;

		//Copy samples
		for(uint i=1; i<count; i += X_SIZE)
		{
			//Prefetch
			prefetchBlock[gl_LocalInvocationID.y][gl_LocalInvocationID.x + 1] =
				offsetsSecondPass[readbase + i + gl_LocalInvocationID.x];
			barrier();
			memoryBarrierShared();

			if(gl_GlobalInvocationID.x != 0)
				continue;

			//Load the 0th entry
			prefetchBlock[gl_LocalInvocationID.y][0] = offsetsSecondPass[readbase + i - 1];

			for(uint j=0; j<X_SIZE && (i+j) < count; j++)
			{
				uint iout = writebase + i + j - 1;
				int64_t lastOffset = prefetchBlock[gl_LocalInvocationID.y][j];
				int64_t nextOffset = prefetchBlock[gl_LocalInvocationID.y][j+1];
				squarewave[iout] = uint8_t(iout & 1);

				//Generate the squarewave output
				uint nsample = min(uint((lastOffset - triggerPhase) / timescale), maxInputSamples-1);
				float sampledData = isamples[nsample];
				offsets[iout] = lastOffset;
				durations[iout] = nextOffset - lastOffset;

				//Generate sampled data output
				ssamples[iout] = sampledData;
			}
		}

		if(gl_GlobalInvocationID.x != 0)
			return;

		//Last sample (if needed)
		uint iout = writebase + count - 1;
		int64_t lastOffset = offsetsSecondPass[readbase + count-1];
		int64_t lastPeriod = stateSecondPass[(gl_GlobalInvocationID.y - 1)*3 + 1];
		int64_t tout = lastOffset;
		offsets[iout] = tout;
		squarewave[iout] = uint8_t(iout & 1);
		durations[iout] = lastPeriod;

		//Generate sampled data output
		uint nsample = min(uint((tout - triggerPhase) / timescale), maxInputSamples-1);
		ssamples[iout] = isamples[nsample];
	}
}
