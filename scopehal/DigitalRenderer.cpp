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
	@brief Implementation of DigitalRenderer
 */

#include "scopehal.h"
#include "ChannelRenderer.h"
#include "DigitalRenderer.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction
DigitalRenderer::DigitalRenderer(OscilloscopeChannel* channel)
: ChannelRenderer(channel)
{
	m_height = 22;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rendering

void DigitalRenderer::RenderSampleCallback(
	const Cairo::RefPtr<Cairo::Context>& cr,
	size_t i,
	float xstart,
	float xend,
	int visleft,
	int visright
	)
{
	float ytop = m_ypos + m_padding;
	float ybot = m_ypos + m_height - 2*m_padding;
	float ymid = (ybot-ytop)/2 + ytop;

	//Scalar channels - lines
	if(m_channel->GetWidth() == 1)
	{
		DigitalCapture* capture = dynamic_cast<DigitalCapture*>(m_channel->GetData());
		if(capture == NULL)
			return;

		const DigitalSample& sample = capture->m_samples[i];
		float tscale = m_channel->m_timescale * capture->m_timescale;
		float rendered_uncertainty = tscale * 0.1;

		//Move to initial position if first sample
		float y = sample.m_sample ? ytop : ybot;
		if(i == 0)
			cr->move_to(xstart, y);

		//Render
		cr->line_to(xstart + rendered_uncertainty, y);
		cr->line_to(xend - rendered_uncertainty, y);
	}

	//Vector channels - text
	else
	{
		DigitalBusCapture* capture = dynamic_cast<DigitalBusCapture*>(m_channel->GetData());
		if(capture == NULL)
			return;

		const DigitalBusSample& sample = capture->m_samples[i];
		float rendered_uncertainty = 5;

		//Format text - hex, 4 bits at a time
		//TODO: support other formats
		std::string str = "";
		int maxbit = sample.m_sample.size() - 1;
		for(int j=0; j<=maxbit; j+=4)
		{
			//Pull the rightmost 4 bits, stopping earlier if we're done
			int val = sample.m_sample[maxbit-j];
			for(int k=1; k<4; k++)
			{
				int nbit = maxbit - (j+k);
				if(nbit >= 0)
					val |= sample.m_sample[nbit] << k;
			}
			char buf[3] = {0};
			snprintf(buf, sizeof(buf), "%01x", val);
			str = buf + str;
		}

		//and render
		Gdk::Color color(m_channel->m_displaycolor);
		RenderComplexSignal(
			cr,
			visleft, visright,
			xstart, xend, rendered_uncertainty,
			ybot, ymid, ytop,
			str,
			color);
	}
}

void DigitalRenderer::RenderEndCallback(
	const Cairo::RefPtr<Cairo::Context>& cr,
	int /*width*/,
	int /*visleft*/,
	int /*visright*/,
	std::vector<time_range>& /*ranges*/)
{
	if(m_channel->GetWidth() == 1)
	{
		Gdk::Color color(m_channel->m_displaycolor);
		cr->set_source_rgb(color.get_red_p(), color.get_green_p(), color.get_blue_p());
		cr->stroke();
	}

	cr->restore();
}
