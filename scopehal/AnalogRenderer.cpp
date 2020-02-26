/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2020 Andrew D. Zonenberg                                                                          *
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
	@author Andrew D. Zonenberg
	@brief Implementation of AnalogRenderer
 */

#include "scopehal.h"
#include "ChannelRenderer.h"
#include "AnalogRenderer.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

AnalogRenderer::AnalogRenderer(OscilloscopeChannel* channel)
: ChannelRenderer(channel)
{
	m_height = 125;
	m_yscale = 1;
	m_yoffset = 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rendering

void AnalogRenderer::RenderStartCallback(
	const Cairo::RefPtr<Cairo::Context>& /*cr*/,
	int /*width*/,
	int /*visleft*/,
	int /*visright*/,
	vector<time_range>& /*ranges*/)
{
	//no longer used, will be removed in future refactoring
}

void AnalogRenderer::RenderSampleCallback(
	const Cairo::RefPtr<Cairo::Context>& /*cr*/,
	size_t /*i*/,
	float /*xstart*/,
	float /*xend*/,
	int /*visleft*/,
	int /*visright*/
	)
{
	//no longer used, will be removed in future refactoring
}

void AnalogRenderer::RenderEndCallback(
	const Cairo::RefPtr<Cairo::Context>& /*cr*/,
	int /*width*/,
	int /*visleft*/,
	int /*visright*/,
	vector<time_range>& /*ranges*/)
{
	//no longer used, will be removed in future refactoring
}

float AnalogRenderer::PickStepSize(float volts_per_half_span, int min_steps, int max_steps)
{
	const float step_sizes[24]=
	{
		//mV per div
		0.001,
		0.0025,
		0.005,

		0.01,
		0.025,
		0.05,

		0.1,
		0.25,
		0.5,

		1,
		2.5,
		5,

		10,
		25,
		50,

		100,
		250,
		500,

		1000,
		2500,
		5000,

		10000,
		25000,
		50000
	};

	for(int i=0; i<24; i++)
	{
		float step = step_sizes[i];
		float steps_per_half_span = volts_per_half_span / step;
		if(steps_per_half_span > max_steps)
			continue;
		if(steps_per_half_span < min_steps)
			continue;
		return step;
	}

	//if no hits
	return 1;
}
