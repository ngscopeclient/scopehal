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

layout(std430, binding=0) restrict readonly buffer buf_edges
{
	int64_t edges[];
};

layout(std430, binding=1) restrict writeonly buffer buf_offsets
{
	int64_t offsets[];
};

layout(std430, binding=2) restrict writeonly buffer buf_stateout
{
	int64_t stateOut[];
};

layout(std430, push_constant) uniform constants
{
	int64_t	initialPeriod;
	int64_t	halfPeriod;
	int64_t fnyquist;
	int64_t	tend;
	uint	nedges;
	uint	maxOffsetsPerThread;
};

layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

void main()
{
	//Initial starting sample indexes for this thread
	uint numThreads = gl_NumWorkGroups.x * gl_WorkGroupSize.x;
	uint numEdgesPerThread = nedges / numThreads;
	uint nStartingEdge = gl_GlobalInvocationID.x * numEdgesPerThread;
	uint nedge = nStartingEdge;
	int64_t edgepos = edges[nStartingEdge];
	nedge ++;

	//Starting frequency
	float initialFrequency = 1.0 / float(initialPeriod);
	float glitchCutoff = float(initialPeriod / 10);
	float fHalfPeriod = float(halfPeriod);

	//End timestamp and edge index for this thread
	int64_t tThreadEnd;
	uint edgemax;
	if(gl_GlobalInvocationID.x == (numThreads - 1))
	{
		tThreadEnd = tend;
		edgemax = nedges - 1;
	}
	else
	{
		edgemax = nStartingEdge + numEdgesPerThread;
		tThreadEnd = edges[edgemax];
	}

	//Output buffer pointers
	uint outputBase = gl_GlobalInvocationID.x * maxOffsetsPerThread;
	uint iout = 0;

	//Initial PLL state
	int64_t tlast = 0;
	int64_t iperiod = initialPeriod;
	float fperiod = float(iperiod);
	for(; (edgepos < tThreadEnd) && (nedge < edgemax); edgepos += iperiod)
	{
		int64_t center = iperiod/2;

		//See if the next edge occurred in this UI.
		//If not, just run the NCO open loop.
		//Allow multiple edges in the UI if the frequency is way off.
		int64_t tnext = edges[nedge];
		while( (tnext + center < edgepos) && (nedge < edgemax) )
		{
			//Find phase error
			int64_t dphase = (edgepos - tnext) - iperiod;
			float fdphase = float(dphase);

			//If we're more than half a UI off, assume this is actually part of the next UI
			if(fdphase > fHalfPeriod)
				fdphase -= fperiod;
			if(fdphase < -fHalfPeriod)
				fdphase += fperiod;

			//Find frequency error
			float uiLen = float(tnext - tlast);
			float fdperiod = 0;
			if(uiLen > glitchCutoff)		//Sanity check: no correction if we have a glitch
			{
				float numUIs = round(uiLen * initialFrequency);
				if(numUIs != 0)	//divide by zero check needed in some cases
				{
					uiLen /= numUIs;
					fdperiod = fperiod - uiLen;
				}
			}

			if(tlast != 0)
			{
				//Frequency and phase error term
				float errorTerm = (fdperiod * 0.006) + (fdphase * 0.002);
				fperiod -= errorTerm;
				iperiod = int64_t(fperiod);

				//HACK: immediate bang-bang phase shift
				int64_t bangbang = int64_t(fperiod * 0.0025);
				if(dphase > 0)
					edgepos -= bangbang;
				else
					edgepos += bangbang;

				if(iperiod < fnyquist)
				{
					//LogWarning("PLL attempted to lock to frequency near or above Nyquist\n");
					nedge = nedges;
					break;
				}
			}

			tlast = tnext;
			tnext = edges[++nedge];
		}

		//Add the sample (90 deg phase offset from the internal NCO)
		//Maybe we don't want to add the phase shift until the final pass?
		offsets[outputBase + iout] = edgepos /*+ center*/;
		iout ++;

		//Bail if we've run out of places to store output (should never happen, just to be safe)
		if(iout >= maxOffsetsPerThread)
			break;
	}

	//Save final stats
	stateOut[gl_GlobalInvocationID.x*2] 	= int64_t(iout);
	stateOut[gl_GlobalInvocationID.x*2 + 1] = iperiod;
}
