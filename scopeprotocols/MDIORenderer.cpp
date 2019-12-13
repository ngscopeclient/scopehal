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
	@brief Declaration of MDIORenderer
 */

#include "scopeprotocols.h"
#include "../scopehal/scopehal.h"
#include "../scopehal/ChannelRenderer.h"
#include "../scopehal/TextRenderer.h"
#include "MDIORenderer.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

MDIORenderer::MDIORenderer(OscilloscopeChannel* channel)
: TextRenderer(channel)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rendering

Gdk::Color MDIORenderer::GetColor(int i)
{
	MDIOCapture* capture = dynamic_cast<MDIOCapture*>(m_channel->GetData());
	if(capture != NULL)
	{
		const MDIOSymbol& s = capture->m_samples[i].m_sample;

		switch(s.m_stype)
		{
			case MDIOSymbol::TYPE_PREAMBLE:
			case MDIOSymbol::TYPE_START:
			case MDIOSymbol::TYPE_TURN:
				return m_standardColors[COLOR_PREAMBLE];

			case MDIOSymbol::TYPE_OP:
				if( (s.m_data == 1) || (s.m_data == 2) )
					return m_standardColors[COLOR_CONTROL];
				else
					return m_standardColors[COLOR_ERROR];

			case MDIOSymbol::TYPE_PHYADDR:
			case MDIOSymbol::TYPE_REGADDR:
				return m_standardColors[COLOR_ADDRESS];

			case MDIOSymbol::TYPE_DATA:
				return m_standardColors[COLOR_DATA];

			case MDIOSymbol::TYPE_ERROR:
				return m_standardColors[COLOR_ERROR];
		}
	}

	//error
	return Gdk::Color("red");
}

string MDIORenderer::GetText(int i)
{
	MDIOCapture* capture = dynamic_cast<MDIOCapture*>(m_channel->GetData());
	if(capture != NULL)
	{
		const MDIOSymbol& s = capture->m_samples[i].m_sample;

		char tmp[32];
		switch(s.m_stype)
		{
			case MDIOSymbol::TYPE_PREAMBLE:
				return "PREAMBLE";
			case MDIOSymbol::TYPE_START:
				return "SOF";
			case MDIOSymbol::TYPE_TURN:
				return "TURN";

			case MDIOSymbol::TYPE_OP:
				if(s.m_data == 1)
					return "WR";
				else if(s.m_data == 2)
					return "RD";
				else
					return "BAD OP";

			case MDIOSymbol::TYPE_PHYADDR:
				snprintf(tmp, sizeof(tmp), "PHY %02x", s.m_data);
				break;

			case MDIOSymbol::TYPE_REGADDR:
				snprintf(tmp, sizeof(tmp), "REG %02x", s.m_data);
				break;

			case MDIOSymbol::TYPE_DATA:
				snprintf(tmp, sizeof(tmp), "%04x", s.m_data);
				break;

			case MDIOSymbol::TYPE_ERROR:
			default:
				return "ERROR";
		}
		return string(tmp);
	}

	return "";
}
