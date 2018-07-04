/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2017 Andrew D. Zonenberg                                                                          *
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

float AnalogRenderer::pixels_to_volts(float p, bool offset)
{
	float plotheight = m_height - 2*m_padding;
	p = ( p / (plotheight * m_yscale) );

	if(offset)
		return p - m_yoffset;
	else
		return p;
}

float AnalogRenderer::volts_to_pixels(float v, bool offset)
{
	float plotheight = m_height - 2*m_padding;
	if(offset)
		v += m_yoffset;
	return v * plotheight * m_yscale;
}

void AnalogRenderer::RenderStartCallback(
	const Cairo::RefPtr<Cairo::Context>& cr,
	int width,
	int visleft,
	int visright,
	vector<time_range>& ranges)
{
	ChannelRenderer::RenderStartCallback(cr, width, visleft, visright, ranges);

	float ytop = m_ypos + m_padding;
	float ybot = m_ypos + m_height - m_padding;
	float plotheight = m_height - 2*m_padding;
	float halfheight = plotheight/2;
	float ymid = halfheight + ytop;

	//Volts from the center line of our graph to the top. May not be the max value in the signal.
	float volts_per_half_span = pixels_to_volts(halfheight, false);

	//Decide what voltage step to use. Pick from a list (in volts)
	float selected_step = PickStepSize(volts_per_half_span);

	//Calculate grid positions
	m_gridmap.clear();
	m_gridmap[0] = 0;
	for(float dv=0; ; dv += selected_step)
	{
		//Need to flip signs on offset so it goes in the right direction all the time
		float yt = ymid - volts_to_pixels(dv + m_yoffset, false);
		float yb = ymid + volts_to_pixels(dv - m_yoffset, false);

		//Stop if we're off the edge
		if( (yb > ybot) && (yt < ytop) )
			break;

		if(yb <= ybot)
			m_gridmap[-dv] = yb;
		if(yt >= ytop)
			m_gridmap[dv] = yt;
	}

	//Center line is solid
	cr->set_source_rgba(0.7, 0.7, 0.7, 1.0);
	cr->move_to(visleft, ymid - volts_to_pixels(0));
	cr->line_to(visright, ymid - volts_to_pixels(0));
	cr->stroke();

	//Dotted lines above and below
	vector<double> dashes;
	dashes.push_back(2);
	dashes.push_back(2);
	cr->set_dash(dashes, 0);
	for(auto it : m_gridmap)
	{
		if(it.second == ymid)	//no dots on center line
			continue;
		cr->move_to(visleft, it.second);
		cr->line_to(visright, it.second);
	}
	cr->stroke();
	cr->unset_dash();
}

void AnalogRenderer::RenderSampleCallback(
	const Cairo::RefPtr<Cairo::Context>& cr,
	size_t i,
	float xstart,
	float xend,
	int /*visleft*/,
	int /*visright*/
	)
{
	float ytop = m_ypos + m_padding;
	float ybot = m_ypos + m_height - m_padding;
	float plotheight = m_height - 2*m_padding;
	float halfheight = plotheight/2;
	float ymid = halfheight + ytop;

	AnalogCapture* capture = dynamic_cast<AnalogCapture*>(m_channel->GetData());
	if(capture == NULL)
		return;

	const AnalogSample& sample = capture->m_samples[i];

	//Calculate position. If the sample would go off the edge of our render, crop it
	//0 volts is by default the center of our display area
	float y = ymid - volts_to_pixels(sample.m_sample);
	if(y < ytop)
		y = ytop;
	if(y > ybot)
		y = ybot;

	//Move to initial position if first sample
	if(i == 0)
		cr->move_to(xstart, y);

	//Draw at the middle
	float xmid = (xend-xstart)/2 + xstart;

	//Render
	cr->line_to(xmid, y);
}

void AnalogRenderer::RenderEndCallback(
	const Cairo::RefPtr<Cairo::Context>& cr,
	int /*width*/,
	int /*visleft*/,
	int visright,
	vector<time_range>& /*ranges*/)
{
	float ytop = m_ypos + m_padding;
	float plotheight = m_height - 2*m_padding;

	//Draw the actual plot
	Gdk::Color color(m_channel->m_displaycolor);
	cr->set_source_rgb(color.get_red_p(), color.get_green_p(), color.get_blue_p());
	cr->stroke();

	//and then the text for the Y axis scale
	DrawVerticalAxisLabels(cr, visright, ytop, plotheight, m_gridmap);

	cr->restore();
}

float AnalogRenderer::PickStepSize(float volts_per_half_span, int min_steps, int max_steps)
{
	const float step_sizes[12]=
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
		5
	};

	for(int i=0; i<12; i++)
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

void AnalogRenderer::DrawVerticalAxisLabels(
	const Cairo::RefPtr<Cairo::Context>& cr,
	int visright,
	float ytop,
	float plotheight,
	map<float, float>& gridmap,
	bool show_units)
{
	//Draw background for the Y axis labels
	int lineheight, linewidth;
	GetStringWidth(cr, "500 mV_x", false, linewidth, lineheight);
	float lmargin = 5;
	float textleft = visright - (linewidth + lmargin);
	cr->set_source_rgba(0, 0, 0, 0.5);
	cr->rectangle(textleft, ytop, linewidth, plotheight);
	cr->fill();

	//Draw text for the Y axis labels
	cr->set_source_rgba(1.0, 1.0, 1.0, 1.0);
	for(auto it : gridmap)
	{
		float v = it.first;
		char tmp[32];

		if(!show_units)
			snprintf(tmp, sizeof(tmp), "%.0f", v);
		else if(fabs(v) < 1)
			snprintf(tmp, sizeof(tmp), "%.0f mV", v*1000);
		else
			snprintf(tmp, sizeof(tmp), "%.2f V", v);

		float y = it.second - lineheight/2;
		if(y < ytop)
			continue;
		if(y > (ytop + plotheight))
			continue;
		DrawString(textleft + lmargin, y, cr, tmp, false);
	}
	cr->begin_new_path();
}
