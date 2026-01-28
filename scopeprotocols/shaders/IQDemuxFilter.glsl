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

#extension GL_ARB_gpu_shader_int64 : require

layout(std430, binding=0) restrict readonly buffer buf_inSamples
{
	float inSamples[];
};

layout(std430, binding=1) restrict readonly buffer buf_inOffsets
{
	int64_t inOffsets[];
};

layout(std430, binding=2) restrict writeonly buffer buf_iSamples
{
	float iSamples[];
};

layout(std430, binding=3) restrict writeonly buffer buf_iOffsets
{
	int64_t iOffsets[];
};

layout(std430, binding=4) restrict writeonly buffer buf_iDurations
{
	int64_t iDurations[];
};

layout(std430, binding=5) restrict writeonly buffer buf_qSamples
{
	float qSamples[];
};

layout(std430, binding=6) restrict writeonly buffer buf_qOffsets
{
	int64_t qOffsets[];
};

layout(std430, binding=7) restrict writeonly buffer buf_qDurations
{
	int64_t qDurations[];
};

layout(std430, push_constant) uniform constants
{
	uint istart;
	uint outlen;
};

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

void main()
{
	uint i = (gl_GlobalInvocationID.y * gl_NumWorkGroups.x * gl_WorkGroupSize.x) + gl_GlobalInvocationID.x;
	if(i >= outlen)
		return;

	uint iin = i*2 + istart;
	uint iout = i;

	//Copy I/Q outputs to output stream
	iSamples[iout] = inSamples[iin];
	qSamples[iout] = inSamples[iin + 1];

	//Copy timestamps
	int64_t tnow = inOffsets[iin];
	iOffsets[iout] = tnow;
	qOffsets[iout] = tnow;

	//Duration
	int64_t len;
	if(i+1 >= outlen)
		len = 1;
	else
		len = inOffsets[iin + 2] - tnow;

	iDurations[iout] = len;
	qDurations[iout] = len;
}
