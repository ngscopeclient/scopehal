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

#extension GL_ARB_gpu_shader_int64 : require
#extension GL_EXT_shader_atomic_int64 : require

layout(std430, binding=0) restrict readonly buffer buf_inI
{
	float din_i[];
};

layout(std430, binding=1) restrict readonly buffer buf_inQ
{
	float din_q[];
};

layout(std430, binding=2) restrict buffer buf_accum
{
	int64_t accum[];
};

layout(std430, binding=3) restrict readonly buffer buf_pointsI
{
	float pointsI[];
};

layout(std430, binding=4) restrict readonly buffer buf_pointsQ
{
	float pointsQ[];
};

layout(std430, binding=5) restrict writeonly buffer buf_evm
{
	float evm[];
};

layout(std430, push_constant) uniform constants
{
	uint		width;
	uint		height;
	uint		inlen;
	uint		samplesPerThread;
	uint		nConstellationPoints;
	float		xmid;
	float		ymid;
	float		xscale;
	float		yscale;
};

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

void main()
{
	//Find the range of samples allocated to each thread
	uint istart = gl_GlobalInvocationID.x * samplesPerThread;
	uint iend = (gl_GlobalInvocationID.x + 1) * samplesPerThread;
	if(istart >= inlen)
	{
		evm[gl_GlobalInvocationID.x] = 0;
		return;
	}
	if(iend >= inlen)
		iend = inlen - 1;

	//Crunch the samples
	float partialSum = 0;
	float c = 0;
	for(uint i=istart; i<iend; i++)
	{
		float ival = din_i[i];
		float qval = din_q[i];

		//EVM calculation
		if(nConstellationPoints > 0)
		{
			//Find the closest simulation point by brute force
			//TODO: can we be more efficient?
			float minvec = 1e10;
			for(uint j=0; j<nConstellationPoints; j++)
			{
				float dx = pointsI[j] - ival;
				float dy = pointsQ[j] - qval;
				minvec = min(minvec, dx*dx + dy*dy);
			}

			//Kahan summation to calculate overall EVM
			float y = sqrt(minvec) - c;
			float t = partialSum + y;
			c = (t - partialSum) - y;
			partialSum = t;
		}

		//Convert to screen coordinates, bounds check, and discard if off the plot
		uint x = uint(round(xmid + xscale*ival));
		uint y = uint(round(ymid + yscale*qval));
		if( (x >= width) || (y >= height) )
			continue;

		//Plot the point
		atomicAdd(accum[y*width + x], 1);
	}

	//Save final EVM
	evm[gl_GlobalInvocationID.x] = partialSum;
}
