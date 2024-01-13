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
	float din[];
};

layout(std430, binding=1) restrict writeonly buffer buf_dout
{
	float dout[];
};

layout(std430, push_constant) uniform constants
{
	uint nblocks;
	uint nouts;
	uint ygrid;
	float logscale;
	float impscale;
	float minscale;
	float irange;
};

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

void main()
{
	const float flog10 = log(10);
	const float logscale = 10 / flog10;

	//If off end of array, stop
	if(gl_GlobalInvocationID.x >= nouts)
		return;

	//Find real Y coordinate
	uint realy = gl_GlobalInvocationID.y*ygrid + gl_GlobalInvocationID.z;
	if(realy >= nblocks)
		return;

	//Rotate spectrogram by half the block size so center frequency is in the middle
	//TODO: why is this needed?
	uint isample = gl_GlobalInvocationID.x + (nouts/2);
	if(isample >= nouts)
		isample -= nouts;

	uint nin = (nouts*realy + isample)*2;
	uint nout = gl_GlobalInvocationID.x*nblocks + realy;

	float real = din[nin];
	float imag = din[nin + 1];

	float vsq = real*real + imag*imag;
	float dbm = (logscale * log(vsq * impscale) + 30);
	if(dbm < minscale)
		dout[nout] = 0;
	else
		dout[nout] = (dbm - minscale) * irange;
}
