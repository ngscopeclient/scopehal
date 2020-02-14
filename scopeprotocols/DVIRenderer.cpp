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
	@brief Declaration of DVIRenderer
 */

#include "scopeprotocols.h"
#include "../scopehal/scopehal.h"
#include "../scopehal/ChannelRenderer.h"
#include "../scopehal/TextRenderer.h"
#include "DVIRenderer.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DVIRenderer::DVIRenderer(OscilloscopeChannel* channel)
: TextRenderer(channel)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rendering

Gdk::Color DVIRenderer::GetColor(int i)
{
	DVICapture* capture = dynamic_cast<DVICapture*>(m_channel->GetData());
	if(capture != NULL)
	{
		const DVISymbol& s = capture->m_samples[i].m_sample;

		switch(s.m_type)
		{
			case DVISymbol::DVI_TYPE_PREAMBLE:
				return m_standardColors[COLOR_PREAMBLE];

			case DVISymbol::DVI_TYPE_HSYNC:
			case DVISymbol::DVI_TYPE_VSYNC:
				return m_standardColors[COLOR_CONTROL];

			case DVISymbol::DVI_TYPE_VIDEO:
				{
					Gdk::Color ret;
					ret.set_rgb_p(s.m_red / 255.0f, s.m_green / 255.0f, s.m_blue / 255.0f);
					return ret;
				}

			case DVISymbol::DVI_TYPE_ERROR:
			default:
				return m_standardColors[COLOR_ERROR];
		}
	}

	//error
	return m_standardColors[COLOR_ERROR];
}

string DVIRenderer::GetText(int i)
{
	DVICapture* capture = dynamic_cast<DVICapture*>(m_channel->GetData());
	if(capture != NULL)
	{
		const DVISymbol& s = capture->m_samples[i].m_sample;

		char tmp[32];
		switch(s.m_type)
		{
			case DVISymbol::DVI_TYPE_PREAMBLE:
				return "BLANK";

			case DVISymbol::DVI_TYPE_HSYNC:
				return "HSYNC";

			case DVISymbol::DVI_TYPE_VSYNC:
				return "VSYNC";

			case DVISymbol::DVI_TYPE_VIDEO:
				snprintf(tmp, sizeof(tmp), "#%02x%02x%02x", s.m_red, s.m_green, s.m_blue);
				break;

			case DVISymbol::DVI_TYPE_ERROR:
			default:
				return "ERROR";

		}
		return string(tmp);
	}
	return "";
}
