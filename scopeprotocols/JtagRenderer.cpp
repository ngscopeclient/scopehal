/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2019 Andrew D. Zonenberg                                                                          *
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
	@brief Implementation of JtagRenderer
 */

#include "scopeprotocols.h"
#include "../scopehal/scopehal.h"
#include "../scopehal/ChannelRenderer.h"
#include "../scopehal/TextRenderer.h"
#include "JtagRenderer.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

JtagRenderer::JtagRenderer(OscilloscopeChannel* channel)
: TextRenderer(channel)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rendering

Gdk::Color JtagRenderer::GetColor(int i)
{
	JtagCapture* capture = dynamic_cast<JtagCapture*>(m_channel->GetData());
	if(capture != NULL)
	{
		const JtagSymbol& s = capture->m_samples[i].m_sample;

		switch(s.m_state)
		{
			//Unknown states
			case JtagSymbol::UNKNOWN_0:
			case JtagSymbol::UNKNOWN_1:
			case JtagSymbol::UNKNOWN_2:
			case JtagSymbol::UNKNOWN_3:
			case JtagSymbol::UNKNOWN_4:
				return m_standardColors[COLOR_ERROR];

			//Data characters
			case JtagSymbol::SHIFT_IR:
			case JtagSymbol::SHIFT_DR:
				return m_standardColors[COLOR_DATA];

			//intermediate states
			default:
				return m_standardColors[COLOR_CONTROL];
		}
	}

	//error
	return m_standardColors[COLOR_ERROR];
}

string JtagRenderer::GetText(int i)
{
	JtagCapture* capture = dynamic_cast<JtagCapture*>(m_channel->GetData());
	if(capture != NULL)
	{
		const JtagSymbol& s = capture->m_samples[i].m_sample;

		char tmp[128];
		const char* sstate = JtagSymbol::GetName(s.m_state);
		if(s.m_len == 0)
			return sstate;
		else if(s.m_len == 8)
		{
			snprintf(tmp, sizeof(tmp), "%02x / %02x", s.m_idata, s.m_odata);
			return tmp;
		}
		else
		{
			snprintf(tmp, sizeof(tmp), "%d'h%02x / %d'h%02x", s.m_len, s.m_idata, s.m_len, s.m_odata);
			return tmp;
		}

	}
	return "";
}
