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

layout(std430, binding=0) restrict readonly buffer buf_clockEdges
{
	int64_t clockEdges[];
};

layout(std430, binding=1) restrict readonly buffer buf_waveform
{
	float waveform[];
};

layout(std430, binding=2) buffer buf_accum
{
	int64_t accum[];
};

layout(std430, binding=3) restrict readonly buffer buf_index
{
	uint index[];
};

layout(std430, push_constant) uniform constants
{
	uint64_t	width;
	uint64_t	halfwidth;
	int64_t		timescale;
	int64_t		triggerPhase;
	int64_t		xoff;
	uint		wend;
	uint 		cend;
	uint 		xmax;
	uint 		ymax;
	uint		mwidth;
	float		xtimescale;
	float		yscale;
	float		yoff;
	float		xscale;
};

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

void main()
{
	//Figure out how many samples are allocated to each thread
	//TODO: is this more efficient to calculate once CPU-side?
	const uint numThreads = gl_NumWorkGroups.x * gl_WorkGroupSize.x;
	const uint numSamplesPerThread = (wend + 1) / numThreads;

	//Find the range of samples allocated to each thread
	uint iclock = index[gl_GlobalInvocationID.x];
	uint istart = gl_GlobalInvocationID.x * numSamplesPerThread;
	uint iend = istart + numSamplesPerThread;
	if(gl_GlobalInvocationID.x == (numThreads - 1))
		iend = wend;

	//Loop over samples for this thread
	float lastSample = waveform[istart];
	int64_t lastClock = clockEdges[iclock];
	int64_t tnext = clockEdges[iclock + 1];
	int64_t tnext_adv = tnext;
	for(uint i=istart; i<iend && iclock < cend; i++)
	{
		//Find time of this sample
		int64_t tstart = int64_t(i) * timescale + triggerPhase;
		tnext = tnext_adv;
		int64_t ttnext = tnext - tstart;

		//Fetch the next sample
		float sampleA = lastSample;
		float sampleB = waveform[i+1];
		lastSample = sampleB;

		//If it's past the end of the current UI, move to the next clock edge
		int64_t offset = tstart - lastClock;
		if(offset < 0)
			continue;
		if(tstart >= tnext)
		{
			//Move to the next clock edge
			iclock ++;
			lastClock = tnext;
			if(iclock >= cend)
				break;

			//Prefetch the next edge timestamp
			tnext_adv = clockEdges[iclock + 1];

			//Figure out the offset to the next edge
			offset = tstart - tnext;
		}

		//Interpolate position
		float pixel_x_f = float(offset - xoff) * xscale;
		float pixel_x_fround = floor(pixel_x_f);
		float dx_frac = (pixel_x_f - pixel_x_fround ) / xtimescale;

		//Drop anything past half a UI if the next clock edge is a long ways out
		//(this is needed for irregularly sampled data like DDR RAM)
		if( (offset > halfwidth) && (ttnext > width) )
			continue;

		//Early out if off end of plot
		uint pixel_x_round = uint(floor(pixel_x_f));
		if(pixel_x_round > xmax)
			continue;

		//Interpolate voltage, early out if clipping
		float dv = sampleB - sampleA;
		float nominal_voltage = sampleA + dv*dx_frac;
		float nominal_pixel_y = nominal_voltage*yscale + yoff;
		uint y1 = uint(nominal_pixel_y);
		if(y1 >= ymax)
			continue;

		//Calculate how much of the pixel's intensity to put in each row
		float yfrac = nominal_pixel_y - floor(nominal_pixel_y);
		int64_t bin2 = int64_t(yfrac * 64.0);
		uint pixidx = y1*mwidth + pixel_x_round;

		//Plot each point (this only draws the right half of the eye, we copy to the left later)
		atomicAdd(accum[pixidx], 64 - bin2);
		atomicAdd(accum[pixidx + mwidth], bin2);
	}
}

