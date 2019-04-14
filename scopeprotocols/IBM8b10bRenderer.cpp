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
	@brief Declaration of IBM8b10bRenderer
 */

#include "scopeprotocols.h"
#include "../scopehal/scopehal.h"
#include "../scopehal/ChannelRenderer.h"
#include "../scopehal/TextRenderer.h"
#include "IBM8b10bRenderer.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

IBM8b10bRenderer::IBM8b10bRenderer(OscilloscopeChannel* channel)
: TextRenderer(channel)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rendering

Gdk::Color IBM8b10bRenderer::GetColor(int i)
{
	IBM8b10bCapture* capture = dynamic_cast<IBM8b10bCapture*>(m_channel->GetData());
	if(capture != NULL)
	{
		const IBM8b10bSymbol& s = capture->m_samples[i].m_sample;

		//errors are red
		if(s.m_error)
			return Gdk::Color("#ff0000");

		//control characters are purple
		else if(s.m_control)
			return Gdk::Color("#c000a0");

		//Data characters are green
		else
			return Gdk::Color("#008000");
	}

	//error
	return Gdk::Color("red");
}

string IBM8b10bRenderer::GetText(int i)
{
	IBM8b10bCapture* capture = dynamic_cast<IBM8b10bCapture*>(m_channel->GetData());
	if(capture != NULL)
	{
		const IBM8b10bSymbol& s = capture->m_samples[i].m_sample;

		unsigned int right = s.m_data >> 5;
		unsigned int left = s.m_data & 0x1F;

		char tmp[32];
		if(s.m_control)
			snprintf(tmp, sizeof(tmp), "K%d.%d", left, right);
		else if(s.m_error)
			snprintf(tmp, sizeof(tmp), "ERR");
		else
			snprintf(tmp, sizeof(tmp), "D%d.%d", left, right);
		return string(tmp);
	}
	return "";
}
