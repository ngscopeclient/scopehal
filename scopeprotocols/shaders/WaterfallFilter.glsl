/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2022 Andrew D. Zonenberg and contributors                                                         *
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

layout(std430, binding=1) restrict readonly buffer buf_din
{
	float din[];
};

layout(std430, binding=2) restrict writeonly buffer buf_dout
{
	float dout[];
};

layout(std430, push_constant) uniform constants
{
	uint width;
	uint height;
	uint inlen;
	float vrange;
	float vfs;
	float timescaleRatio;
};

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

void main()
{
	//Bounds check
	if(gl_GlobalInvocationID.x >= width)
		return;
	if(gl_GlobalInvocationID.y >= height)
		return;

	//Lower rows move down
	if(gl_GlobalInvocationID.y < (height-1) )
	{
		dout[gl_GlobalInvocationID.y * width + gl_GlobalInvocationID.x] =
			din[(gl_GlobalInvocationID.y+1) * width + gl_GlobalInvocationID.x];
	}

	//Topmost row gets new content
	else
	{
		float vmin = 1.0 / 255.0;

		uint binMin = uint(round(gl_GlobalInvocationID.x * timescaleRatio));
		uint binMax = uint(round((gl_GlobalInvocationID.x+1) * timescaleRatio)) - 1;

		float maxAmplitude = vmin;
		for(uint i=binMin; (i <= binMax) && (i <= inlen); i++)
		{
			float v = 1 - ( (dnew[i] - vfs) / -vrange);
			maxAmplitude = max(maxAmplitude, v);
		}

		dout[gl_GlobalInvocationID.y * width + gl_GlobalInvocationID.x] = maxAmplitude;
	}

}
