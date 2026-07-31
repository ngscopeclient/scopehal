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

layout(std430, binding=0) restrict readonly buffer buf_dnew
{
	float dnew[];
};

layout(std430, binding=1) restrict buffer buf_dout
{
	float dout[];
};

layout(std430, push_constant) uniform constants
{
	uint width;
	uint height;
	uint inlen;
	uint writerow;
	float vrange;
	float vfs;
	float timescaleRatio;
};

#define X_SIZE 16
#define Y_SIZE 64

shared float g_max[X_SIZE][Y_SIZE];

layout(local_size_x=X_SIZE, local_size_y=Y_SIZE, local_size_z=1) in;

void main()
{
	//Bounds check
	uint xpos = (gl_GlobalInvocationID.z * gl_NumWorkGroups.y * gl_WorkGroupSize.y) + gl_GlobalInvocationID.y;
	if(xpos >= width)
		return;

	//Write new data at the write position, downsampling the full FFT width to fit the output buffer
	float vmin = 1.0 / 255.0;

	uint binMin = uint(round(xpos * timescaleRatio));
	uint binMax = uint(round((xpos+1) * timescaleRatio)) - 1;

	//Parallel max search
	float maxAmplitude = vmin;
	for(uint i=binMin + gl_LocalInvocationID.x; (i <= binMax) && (i <= inlen); i += X_SIZE)
	{
		float v = 1 - ( (dnew[i] - vfs) / -vrange);
		maxAmplitude = max(maxAmplitude, v);
	}
	g_max[gl_LocalInvocationID.x][gl_LocalInvocationID.y] = maxAmplitude;

	memoryBarrierShared();
	barrier();

	if(gl_LocalInvocationID.x == 0)
	{
		maxAmplitude = 0;
		for(int i=0; i<X_SIZE; i++)
			maxAmplitude = max(maxAmplitude, g_max[i][gl_LocalInvocationID.y]);

		dout[writerow * width + xpos] = maxAmplitude;
	}
}
