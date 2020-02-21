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
	@brief Declaration of TMDSRenderer
 */

#include "scopeprotocols.h"
#include "../scopehal/scopehal.h"
#include "../scopehal/ChannelRenderer.h"
#include "../scopehal/TextRenderer.h"
#include "TMDSRenderer.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

TMDSRenderer::TMDSRenderer(OscilloscopeChannel* channel)
: TextRenderer(channel)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rendering

Gdk::Color TMDSRenderer::GetColor(int i)
{
	TMDSCapture* capture = dynamic_cast<TMDSCapture*>(m_channel->GetData());
	if(capture != NULL)
	{
		const TMDSSymbol& s = capture->m_samples[i].m_sample;

		switch(s.m_type)
		{
			case TMDSSymbol::TMDS_TYPE_CONTROL:
				return m_standardColors[COLOR_CONTROL];

			case TMDSSymbol::TMDS_TYPE_GUARD:
				return m_standardColors[COLOR_PREAMBLE];

			case TMDSSymbol::TMDS_TYPE_DATA:
				return m_standardColors[COLOR_DATA];

			case TMDSSymbol::TMDS_TYPE_ERROR:
			default:
				return m_standardColors[COLOR_ERROR];
		}
	}

	//error
	return m_standardColors[COLOR_ERROR];
}

string TMDSRenderer::GetText(int i)
{
	TMDSCapture* capture = dynamic_cast<TMDSCapture*>(m_channel->GetData());
	if(capture != NULL)
	{
		const TMDSSymbol& s = capture->m_samples[i].m_sample;

		char tmp[32];
		switch(s.m_type)
		{
			case TMDSSymbol::TMDS_TYPE_CONTROL:
				snprintf(tmp, sizeof(tmp), "CTL%d", s.m_data);
				break;

			case TMDSSymbol::TMDS_TYPE_GUARD:
				return "GB";

			case TMDSSymbol::TMDS_TYPE_DATA:
				snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
				break;

			case TMDSSymbol::TMDS_TYPE_ERROR:
			default:
				return "ERROR";

		}
		return string(tmp);
	}
	return "";
}
