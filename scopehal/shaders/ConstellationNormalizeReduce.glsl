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
#extension GL_EXT_shader_atomic_int64 : require

layout(std430, binding=0) restrict buffer buf_accumData
{
	int64_t accumData[];
};

layout(std430, binding=1) buffer buf_reduceData
{
	int64_t nmax;
};

layout(std430, push_constant) uniform constants
{
	uint	width;
	uint	height;
	float	satLevel;
};

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

void main()
{
	//thread X coordinate is actually our Y position, weird but that's how it worked out lol

	//If out of bounds, stop without doing anything else
	if(gl_GlobalInvocationID.x >= height)
		return;

	//Clear the output to zero
	if(gl_GlobalInvocationID.x == 0)
		nmax = 0;
	memoryBarrier();

	int64_t nmaxTemp = 0;

	//Loop over pixels
	uint offsetRead = gl_GlobalInvocationID.x * width;
	for(uint x=0; x < width; x ++)
	{
		//Find peak amplitude
		int64_t readPixel = accumData[offsetRead + x];
		nmaxTemp = max(readPixel, nmaxTemp);
	}

	//Write it back
	atomicMax(nmax, nmaxTemp);
}

