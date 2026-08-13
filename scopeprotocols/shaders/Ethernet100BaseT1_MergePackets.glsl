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
#extension GL_ARB_gpu_shader_int64 : require

layout(std430, binding=0) restrict readonly buffer buf_startsIn
{
	uint startsIn[];
};

layout(std430, binding=1) restrict readonly buffer buf_scramblersIn
{
	uint64_t scramblersIn[];
};

layout(std430, binding=2) restrict writeonly buffer buf_startsOut
{
	uint startsOut[];
};

layout(std430, binding=3) restrict writeonly buffer buf_scramblersOut
{
	uint64_t scramblersOut[];
};

layout(std430, binding=4) restrict writeonly buffer buf_npackets
{
	uint64_t npackets[];
};

layout(std430, push_constant) uniform constants
{
	uint nthreads;
	uint maxOutputPerThread;
};

#define X_SIZE 128

layout(local_size_x=X_SIZE, local_size_y=1, local_size_z=1) in;

shared uint s_packetsFromThread[X_SIZE];

void main()
{
	//We only run a single work group for now
	uint nthread = gl_GlobalInvocationID.x;

	//Output packet count
	uint nouts = 0;

	for(uint i=0; i<nthreads; i += X_SIZE)
	{
		//Parallel read the size of each block and cache in shared memory
		uint ireal = i + gl_LocalInvocationID.x;
		uint outbase = ireal * maxOutputPerThread;
		uint numEntries = startsIn[outbase];

		//If we get bogus output, set size to zero
		if(numEntries > maxOutputPerThread)
			numEntries = 0;

		s_packetsFromThread[gl_LocalInvocationID.x] = numEntries;

		//Add up the total number of packets before our start position so we know where to write
		barrier();
		for(uint j=0; j<gl_LocalInvocationID.x; j++)
			nouts += s_packetsFromThread[j];

		//Actually copy the outputs
		for(uint j=0; j<numEntries; j++)
		{
			startsOut[nouts + j] 		= startsIn[outbase + j + 1];
			scramblersOut[nouts + j]	= scramblersIn[outbase + j + 1];
		}

		//Add our packet count, plus subsequent ones, so next block starts at the right spot
		for(uint j=gl_LocalInvocationID.x; j<X_SIZE; j++)
			nouts += s_packetsFromThread[j];

		barrier();
	}

	if(gl_GlobalInvocationID.x == 0)
		npackets[0] = nouts;
}
