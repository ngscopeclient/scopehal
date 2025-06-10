/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
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

/**
	@file
	@brief Cycle by cycle AC RMS calculation
 */

#version 430
#pragma shader_stage(compute)
#extension GL_ARB_gpu_shader_int64 : require

layout(std430, binding=0) restrict writeonly buffer buf_poutSamples
{
	float poutSamples[];
};

layout(std430, binding=1) restrict writeonly buffer buf_poutOffsets
{
	int64_t poutOffsets[];
};

layout(std430, binding=2) restrict writeonly buffer buf_poutDurations
{
	int64_t poutDurations[];
};

layout(std430, binding=3) restrict readonly buffer buf_pin
{
	float pin[];
};

layout(std430, binding=4) restrict readonly buffer buf_pedges
{
	int64_t pedges[];
};

layout(std430, push_constant) uniform constants
{
	int64_t timescale;
	int64_t numSamples;
	uint numEdgePairs;
	float dcBias;
};

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

void main()
{
	uint nthread = (gl_GlobalInvocationID.y * gl_NumWorkGroups.x * gl_WorkGroupSize.x) + gl_GlobalInvocationID.x;
	if(nthread > numEdgePairs)
		return;

	//We work on pairs of edges
	uint i = nthread * 2;

	//Measure from edge to 2 edges later, since we find all zero crossings regardless of polarity
	int64_t start = pedges[i] / timescale;
	int64_t end = pedges[i + 2] / timescale;
	int64_t j = 0;

	//Simply sum the squares of all values in a cycle after subtracting the DC value
	//TODO: Kahan summation or do we assume not a lot of samples in one cycle?
	float temp = 0;
	for(j = start; (j <= end) && (j < numSamples); j++)
	{
		float delta = pin[uint(j)] - dcBias;
		temp += delta * delta;
	}

	//Get the difference between the end and start of cycle. This would be the number of samples
	//on which AC RMS calculation was performed
	int64_t delta = j - start - 1;

	//Divide by total number of samples for one cycle (with divide-by-zero check for garbage input)
	if (delta == 0)
		temp = 0;
	else
		temp /= float(delta);

	poutSamples[nthread] = sqrt(temp);
	poutOffsets[nthread] = start;
	poutDurations[nthread] = delta;
}

