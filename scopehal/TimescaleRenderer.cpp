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
	@brief Implementation of TimescaleRenderer
 */

#include "scopehal.h"
#include "ChannelRenderer.h"
#include "TimescaleRenderer.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction
TimescaleRenderer::TimescaleRenderer(OscilloscopeChannel* pChannel)
: ChannelRenderer(pChannel)
{
	m_height = 30;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rendering

void TimescaleRenderer::Render(const Cairo::RefPtr<Cairo::Context>& cr, int width, int visleft, int visright, std::vector<time_range>& ranges)
{
	cr->save();

	//Cache some coordinates
	double ytop = m_ypos + m_padding;
	double ybot = m_ypos + m_height - m_padding;
	double ymid = m_ypos + (m_height / 2);

	m_width = 0;

	//Set the color
	Gdk::Color color("white");
	cr->set_source_rgb(color.get_red_p(), color.get_green_p(), color.get_blue_p());

	CaptureChannelBase* capture = m_channel->GetData();
	if(capture != NULL)
	{
		//Draw top line
		cr->move_to(0, m_ypos);
		cr->line_to(width, m_ypos);
		cr->stroke();

		//Save time scales
		double tscale = m_channel->m_timescale * capture->m_timescale;

		//Figure out about how much time per graduation to use
		const int min_label_grad_width = 100;		//Minimum distance between text labels, in pixels
		int64_t ps_per_grad = min_label_grad_width / m_channel->m_timescale;

		//Round up to the nearest multiple of 5
		double grad_log_ps = log(ps_per_grad) / log(10);
		double grad_log_ps_rounded = ceil(grad_log_ps);
		int64_t grad_ps_rounded = pow(10, grad_log_ps_rounded);
		if( (grad_ps_rounded/2) > ps_per_grad )
			grad_ps_rounded /= 2;

		//Figure out what units to use, based on the length of the capture
		int64_t tend = capture->m_timescale * capture->GetEndTime();
		const char* units = "ps";
		int64_t unit_divisor = 1;
		if(tend < 100)
		{
			//ps, leave default
		}
		else if(tend < 1E5)
		{
			units = "ns";
			unit_divisor = 1E3;
		}
		else if(tend < 1E8)
		{
			units = "μs";
			unit_divisor = 1E6;
		}
		else if(tend < 1E11)
		{
			units = "ms";
			unit_divisor = 1E9;
		}
		else
		{
			units = "s";
			unit_divisor = 1E12;
		}

		//If the last range ends before the end of the window, make the scale go all the way out
		if( !ranges.empty() && (ranges[ranges.size() - 1].xend < visright) )
		{
			time_range r = ranges[ranges.size() - 1];

			double dx = (visright - r.xstart) / tscale;
			tend = (dx + r.tstart) * capture->m_timescale;
		}

		//Render ranges
		for(size_t i=0; i<ranges.size(); i++)
		{
			time_range r = ranges[i];

			//Skip the range if it's totally offscreen
			if( (r.xend < visleft) || (r.xstart > visright) )
				continue;

			//Round start time up to nearest multiple of samples_per_div
			double samples_per_div = static_cast<double>(grad_ps_rounded) / capture->m_timescale;
			int64_t tstart_rounded =
				ceil(static_cast<double>(r.tstart) / samples_per_div) * samples_per_div;

			double tend_adj = r.tend;
			if(i == (ranges.size() - 1) )
				tend_adj = tend / capture->m_timescale;	//go to end of screen on last one
			double xend_adj = (static_cast<double>(tend_adj - r.tstart) * tscale) + r.xstart;

			//Clamp end to start of next range
			if( (i+1) < ranges.size() )
			{
				double nstart = ranges[i+1].xstart;
				if(xend_adj > nstart)
					xend_adj = nstart;
			}

			//Draw initial tickmarks
			int nsubticks = 1;
			if( (grad_ps_rounded / 2) >= capture->m_timescale)
				nsubticks = 2;
			if( (grad_ps_rounded / 5) >= capture->m_timescale)
				nsubticks = 5;
			if( (grad_ps_rounded / 10) >= capture->m_timescale)
				nsubticks = 10;
			double subtick = samples_per_div / nsubticks;
			for(int tick=1; tick < nsubticks; tick++)
			{
				double to = tstart_rounded - r.tstart;
				double x = (to*tscale) + r.xstart;
				double subx = x - (tick * subtick * tscale);
				if(subx < r.xstart)
					continue;
				if(subx > xend_adj)
					continue;
				cr->move_to(subx, ytop);
				cr->line_to(subx, ymid);
				cr->stroke();
			}

			//Print tick marks and labels
			for(double t = tstart_rounded; t < tend_adj; t += samples_per_div)
			{
				double to = t - r.tstart;	//offset since start of this range
				double x = (to*tscale) + r.xstart;

				if(x < visleft)
					continue;

				if(x > visright)
					break;
				if(x > xend_adj)
					continue;

				double tscaled = t * capture->m_timescale;

				//Tick mark
				cr->move_to(x, ytop);
				cr->line_to(x, ybot);
				cr->stroke();

				//Format the string
				double scaled_time = static_cast<double>(tscaled) / unit_divisor;
				char namebuf[256];
				if(tscale*samples_per_div > 100)
					snprintf(namebuf, sizeof(namebuf), "%.6lf %s", scaled_time, units);
				else if(tscale*samples_per_div > 75)
					snprintf(namebuf, sizeof(namebuf), "%.4lf %s", scaled_time, units);
				else
					snprintf(namebuf, sizeof(namebuf), "%.2lf %s", scaled_time, units);

				//Render it
				int swidth = 0, sheight = 0;
				GetStringWidth(cr, namebuf, true, swidth, sheight);
				if( (x + 2 + swidth) < xend_adj)
					DrawString(x + 2, ymid + sheight/2, cr, namebuf, false);

				//Draw fine ticks
				for(int tick=1; tick < nsubticks; tick++)
				{
					double subx = x + (tick * subtick * tscale);
					if(subx > xend_adj)
						break;
					cr->move_to(subx, ytop);
					cr->line_to(subx, ymid);
					cr->stroke();
				}
			}
		}
	}
	cr->restore();
}

void TimescaleRenderer::RenderSampleCallback(
		const Cairo::RefPtr<Cairo::Context>& /*cr*/,
		size_t /*i*/,
		float /*xstart*/,
		float /*xend*/,
		int /*visleft*/,
		int /*visright*/)
{
}
