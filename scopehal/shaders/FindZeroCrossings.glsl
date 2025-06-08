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

#version 450
#pragma shader_stage(compute)
#extension GL_ARB_gpu_shader_int64 : require

layout(std430, binding=0) restrict writeonly buffer buf_pout
{
	int64_t pout[];
};

layout(std430, binding=1) restrict readonly buffer buf_pin
{
	float pin[];
};

layout(std430, push_constant) uniform constants
{
	int64_t triggerPhase;	//Trigger timestamp offset for the input
	int64_t timescale;		//Input waveform timebase units per tick
	uint inputSize;			//Total number of input samples
	uint inputPerThread;	//Number of input samples handled by one thread
	uint outputPerThread;	//Number of output samples handled by one thread
							//(must be 1+inputPerThread to allow for the size field in the first slot)
	float threshold;
};

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

float InterpolateTime(float fa, float fb, float voltage);

/**
	@brief First-pass zero crossing detection

	Each thread independently processes a block of inputPerThread samples and outputs a variable-length block, from
	0 to outputPerThread-1 in size, of samples
 */
void main()
{
	//Find our block of inputs
	uint nthread = (gl_GlobalInvocationID.y * gl_NumWorkGroups.x * gl_WorkGroupSize.x) + gl_GlobalInvocationID.x;
	uint instart = nthread * inputPerThread;
	uint inend = instart + inputPerThread;
	if(inend > inputSize)
		inend = inputSize;

	//Find our block of outputs
	int nouts = 0;
	uint outstart = nthread * outputPerThread;
	uint iout = outstart + 1;

	float fscale = float(timescale);

	//Search for level crossings within our block
	for(uint i=instart; i<inend; i++)
	{
		//If this is the first sample, we can't find an edge by definition
		if(i == 0)
			continue;

		float fa = pin[i-1];
		float fb = pin[i];

		bool prevValue = fa > threshold;
		bool currentValue = fb > threshold;

		if(currentValue != prevValue)
		{
			float tfrac = fscale * InterpolateTime(fa, fb, threshold);

			pout[iout] = triggerPhase + timescale*int64_t(i-1) + int64_t(tfrac);
			iout ++;
			nouts ++;
		}
	}

	//Save number of outputs we found
	pout[outstart] = nouts;
}

float InterpolateTime(float fa, float fb, float voltage)
{
	//If the voltage isn't between the two points, abort
	bool ag = (fa > voltage);
	bool bg = (fb > voltage);
	if( (ag && bg) || (!ag && !bg) )
		return 0;

	//no need to divide by time, sample spacing is normalized to 1 timebase unit
	float slope = (fb - fa);
	float delta = voltage - fa;
	return delta / slope;
}
