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
	@brief Declaration of I2CRenderer
 */

#include "scopeprotocols.h"
#include "../scopehal/scopehal.h"
#include "../scopehal/ChannelRenderer.h"
#include "../scopehal/TextRenderer.h"
#include "I2CRenderer.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

I2CRenderer::I2CRenderer(OscilloscopeChannel* channel)
: TextRenderer(channel)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rendering

Gdk::Color I2CRenderer::GetColor(int i)
{
	I2CCapture* capture = dynamic_cast<I2CCapture*>(m_channel->GetData());
	if(capture != NULL)
	{
		const I2CSymbol& s = capture->m_samples[i].m_sample;

		switch(s.m_stype)
		{
			case I2CSymbol::TYPE_ERROR:
				return m_standardColors[COLOR_ERROR];
			case I2CSymbol::TYPE_ADDRESS:
				return m_standardColors[COLOR_ADDRESS];
			case I2CSymbol::TYPE_DATA:
				return m_standardColors[COLOR_DATA];
			default:
				return m_standardColors[COLOR_CONTROL];
		}
	}

	//error
	return m_standardColors[COLOR_ERROR];
}

string I2CRenderer::GetText(int i)
{
	I2CCapture* capture = dynamic_cast<I2CCapture*>(m_channel->GetData());
	if(capture != NULL)
	{
		const I2CSymbol& s = capture->m_samples[i].m_sample;

		char tmp[32];
		switch(s.m_stype)
		{
			case I2CSymbol::TYPE_NONE:
			case I2CSymbol::TYPE_ERROR:
				snprintf(tmp, sizeof(tmp), "ERR");
				break;
			case I2CSymbol::TYPE_START:
				snprintf(tmp, sizeof(tmp), "START");
				break;
			case I2CSymbol::TYPE_RESTART:
				snprintf(tmp, sizeof(tmp), "RESTART");
				break;
			case I2CSymbol::TYPE_STOP:
				snprintf(tmp, sizeof(tmp), "STOP");
				break;
			case I2CSymbol::TYPE_ACK:
				if(s.m_data)
					snprintf(tmp, sizeof(tmp), "NAK");
				else
					snprintf(tmp, sizeof(tmp), "ACK");
				break;
			case I2CSymbol::TYPE_ADDRESS:
				if(s.m_data & 1)
					snprintf(tmp, sizeof(tmp), "R:%02x", s.m_data & 0xfe);
				else
					snprintf(tmp, sizeof(tmp), "W:%02x", s.m_data & 0xfe);
				break;
			case I2CSymbol::TYPE_DATA:
				snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
				break;
		}
		return string(tmp);
	}
	return "";
}
