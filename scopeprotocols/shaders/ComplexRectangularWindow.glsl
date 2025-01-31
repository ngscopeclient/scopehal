/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2024 Andrew D. Zonenberg and contributors                                                         *
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
	float dinI[];
};

layout(std430, binding=1) restrict writeonly buffer buf_dout
{
	float dout[];
};

//bound at 2 rather than 1 to keep commonality with real valued window functions
layout(std430, binding=2) restrict readonly buffer buf_din_Q
{
	float dinQ[];
};

layout(std430, push_constant) uniform constants
{
	uint numActualSamples;
	uint npoints;
	uint offsetIn;
	uint offsetOut;

	//not used in rectangular window, only for interface compatibility
	float scale;
	float alpha0;
	float alpha1;
};

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

void main()
{
	uint i = (gl_GlobalInvocationID.y * 32768) + gl_GlobalInvocationID.x;

	uint inbase = i + offsetIn;
	uint outbase = i + offsetOut;

	//If off end of array, stop
	if(i >= npoints)
		return;

	//If off end of input, zero fill
	else if(i >= numActualSamples)
	{
		dout[outbase*2 + 0] = 0;
		dout[outbase*2 + 1] = 0;
	}

	//Nope, copy it
	else
	{
		dout[outbase*2 + 0] = dinI[inbase];
		dout[outbase*2 + 1] = dinQ[inbase];
	}
}
