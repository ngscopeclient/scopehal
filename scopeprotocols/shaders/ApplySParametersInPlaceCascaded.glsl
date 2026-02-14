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

layout(std430, binding=0) restrict buffer buf_data
{
	float data[];
};

layout(std430, binding=1) restrict readonly buffer buf_sinesA
{
	float sinesA[];
};

layout(std430, binding=2) restrict readonly buffer buf_cosinesA
{
	float cosinesA[];
};

layout(std430, binding=3) restrict readonly buffer buf_sinesB
{
	float sinesB[];
};

layout(std430, binding=4) restrict readonly buffer buf_cosinesB
{
	float cosinesB[];
};

layout(std430, push_constant) uniform constants
{
	uint len;
};

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

void main()
{
	//If off end of array, stop
	uint nthread = (gl_GlobalInvocationID.y * gl_NumWorkGroups.x * gl_WorkGroupSize.x) + gl_GlobalInvocationID.x;
	if(nthread >= len)
		return;

	//Sin/cos values from rotation matrix
	float sinval = sinesA[nthread];
	float cosval = cosinesA[nthread];

	//Uncorrected complex value
	float real_orig = data[nthread*2 + 0];
	float imag_orig = data[nthread*2 + 1];

	//First pass
	float real_A = real_orig*cosval - imag_orig*sinval;
	float imag_A = real_orig*sinval + imag_orig*cosval;

	//Second complex value
	sinval = sinesB[nthread];
	cosval = cosinesB[nthread];

	//Apply the matrix and write back over the original value
	data[nthread*2 + 0] = real_A*cosval - imag_A*sinval;
	data[nthread*2 + 1] = real_A*sinval + imag_A*cosval;
}
