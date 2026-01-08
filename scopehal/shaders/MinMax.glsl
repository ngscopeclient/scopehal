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

layout(std430, binding=0) restrict readonly buffer buf_pin
{
	float pin[];
};

layout(std430, binding=1) restrict writeonly buffer buf_pmin
{
	float pmin[];
};

layout(std430, binding=2) restrict writeonly buffer buf_pmax
{
	float pmax[];
};

layout(std430, push_constant) uniform constants
{
	uint size;
};

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

void main()
{
	//Divide work up into blocks
	uint numThreads = gl_NumWorkGroups.x * gl_WorkGroupSize.x;
	uint nthread = gl_GlobalInvocationID.x;
	uint samplesPerThread = size / numThreads;

	//Find our thread's block
	uint istart = nthread * samplesPerThread;
	uint iend = istart + samplesPerThread;
	if(nthread == numThreads - 1)
		iend = size;

	//Start with min/max at our first sample
	float fmin = pin[istart];
	float fmax = pin[istart];

	//Loop over our block of samples
	for(uint i=istart+1; i < iend; i ++)
	{
		float f = pin[i];
		fmin = min(fmin, f);
		fmax = max(fmax, f);
	}

	//Write reduced output
	pmin[nthread] = fmin;
	pmax[nthread] = fmax;
}
