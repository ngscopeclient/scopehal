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

layout(std430, binding=0) restrict readonly buffer buf_clockEdges
{
	int64_t clockEdges[];
};

layout(std430, binding=1) restrict readonly buffer buf_goldenOffsets
{
	int64_t golden[];
};

layout(std430, binding=2) restrict writeonly buffer buf_out
{
	int64_t dout[];
};

layout(std430, push_constant) uniform constants
{
	int64_t	skip_time;
	uint	nedges;
	uint	ngolden;
	uint	blockBufferSize;
	uint	maxEdgesPerThread;
};

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

void main()
{
	//Each thread processes an equal-sized group of edges in the input signal (not the golden).
	//Get the timestamp of our first edge
	uint numThreads = gl_NumWorkGroups.x * gl_WorkGroupSize.x;
	uint i = gl_GlobalInvocationID.x * maxEdgesPerThread;
	uint outputBase = gl_GlobalInvocationID.x * blockBufferSize;
	if(i > nedges)
	{
		dout[outputBase] = 0;
		return;
	}
	int64_t target = clockEdges[i];

	//Binary search for the first golden clock edge after this sample
	uint pos = ngolden/2;
	uint last_lo = 0;
	uint last_hi = ngolden-1;
	uint iedge = 0;
	if(ngolden > 0)
	{
		//Clip if out of range
		if(golden[0] >= target)
			iedge = 0;
		else if(golden[last_hi] < target)
			iedge = ngolden - 1;

		//Main loop
		else
		{
			while(true)
			{
				//Stop if we've bracketed the target
				if( (last_hi - last_lo) <= 1)
				{
					iedge = last_lo;
					break;
				}

				//Move down
				if(golden[pos] > target)
				{
					uint delta = pos - last_lo;
					last_hi = pos;
					pos = last_lo + delta/2;
				}

				//Move up
				else
				{
					uint delta = last_hi - pos;
					last_lo = pos;
					pos = last_hi - delta/2;
				}
			}
		}
	}

	//We actually want the clock edge before the target, so decrement it unless it's the first in the capture
	if(iedge > 0)
		iedge --;

	//Index of the next output sample
	uint nOutputEdge = 0;

	//End of the input range
	uint lastEdge = min(i + maxEdgesPerThread, nedges);

	//Main loop
	for(; i < lastEdge; i ++)
	{
		int64_t atime = clockEdges[i];

		//Ran out of golden clock edges? Stop
		if(iedge >= ngolden)
			break;

		int64_t prev_edge = golden[iedge];
		int64_t next_edge = prev_edge;
		uint jedge = iedge + 1;

		bool hit = false;

		//Look for a pair of edges bracketing our edge
		while(true)
		{
			prev_edge = next_edge;
			next_edge = golden[jedge];

			//First golden edge is after this signal edge
			if(prev_edge > atime)
				break;

			//Bracketed
			if( (prev_edge < atime) && (next_edge > atime) )
			{
				hit = true;
				break;
			}

			//No, keep looking
			jedge ++;

			//End of capture
			if(jedge >= ngolden)
				break;
		}

		//No interval error possible without a reference clock edge.
		if(!hit)
			continue;

		//Hit! We're bracketed. Start the next search from this edge
		iedge = jedge;

		//Since the CDR filter adds a 90 degree phase offset for sampling in the middle of the data eye,
		//we need to use the *midpoint* of the golden clock cycle as the nominal position of the clock
		//edge for TIE measurements.
		int64_t golden_period = next_edge - prev_edge;
		int64_t golden_center = prev_edge + golden_period/2;

		//Ignore edges before things have stabilized
		if(prev_edge < skip_time)
		{}

		else
		{
			//Add a new sample
			//Worry about durations later. We just store offset and TIE
			dout[2*nOutputEdge + outputBase + 1] = golden_center;
			dout[2*nOutputEdge + outputBase + 2] = atime - golden_center;
			nOutputEdge ++;
		}
	}

	//Store the total number of samples written
	dout[outputBase] = int64_t(nOutputEdge);
}
