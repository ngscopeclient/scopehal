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

#version 430
#pragma shader_stage(compute)

layout(std430, binding=0) restrict readonly buffer buf_din
{
	float din[];
};

layout(std430, binding=1) restrict readonly buffer buf_fkernel
{
	float fkernel[];
};

layout(std430, binding=2) restrict writeonly buffer buf_dout
{
	float dout[];
};

layout(std430, push_constant) uniform constants
{
	uint imax;
	uint upsample_factor;
	uint kernel;
};

layout(local_size_x=1, local_size_y=64, local_size_z=1) in;

void main()
{
	uint i = (gl_GlobalInvocationID.z * gl_NumWorkGroups.y * gl_WorkGroupSize.y) + gl_GlobalInvocationID.y;

	if(i >= imax)
		return;
	if(gl_GlobalInvocationID.x >= upsample_factor)
		return;

	uint offset = i*upsample_factor;

	uint start = 0;
	uint sstart = 0;
	if(gl_GlobalInvocationID.x > 0)
	{
		sstart = 1;
		start = upsample_factor - gl_GlobalInvocationID.x;
	}

	float f = 0;
	for(uint k = start; k<kernel; k += upsample_factor, sstart ++)
		f += fkernel[k] * din[i + sstart];

	dout[offset + gl_GlobalInvocationID.x] = f;
}
