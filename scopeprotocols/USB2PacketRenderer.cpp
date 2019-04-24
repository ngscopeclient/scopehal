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
	@brief Implementation of USB2PacketRenderer
 */

#include "../scopehal/scopehal.h"
#include "../scopehal/ChannelRenderer.h"
#include "../scopehal/TextRenderer.h"
#include "USB2PacketRenderer.h"
#include "USB2PacketDecoder.h"
#include "USBLineStateDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

USB2PacketRenderer::USB2PacketRenderer(OscilloscopeChannel* channel)
	: TextRenderer(channel)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rendering

Gdk::Color USB2PacketRenderer::GetColor(int i)
{
	USB2PacketCapture* data = dynamic_cast<USB2PacketCapture*>(m_channel->GetData());
	if(data == NULL)
		return Gdk::Color("#000000");
	if(i >= (int)data->m_samples.size())
		return Gdk::Color("#000000");

	//TODO: have a set of standard colors we use everywhere?

	auto sample = data->m_samples[i];
	switch(sample.m_sample.m_type)
	{
		case USB2PacketSymbol::TYPE_IDLE:
			return Gdk::Color("#404040");
		case USB2PacketSymbol::TYPE_SYNC:
			return Gdk::Color("#808080");
		case USB2PacketSymbol::TYPE_EOP:
			return Gdk::Color("#808080");
		case USB2PacketSymbol::TYPE_RESET:
			return Gdk::Color("#ffa000");
		case USB2PacketSymbol::TYPE_DATA:
			return Gdk::Color("#336699");

		//invalid state, should never happen
		case USB2PacketSymbol::TYPE_ERROR:
		default:
			return Gdk::Color("#ff0000");
	}
}

string USB2PacketRenderer::GetText(int i)
{
	USB2PacketCapture* data = dynamic_cast<USB2PacketCapture*>(m_channel->GetData());
	if(data == NULL)
		return "";
	if(i >= (int)data->m_samples.size())
		return "";

	auto sample = data->m_samples[i];
	switch(sample.m_sample.m_type)
	{
		case USB2PacketSymbol::TYPE_IDLE:
			return "IDLE";
		case USB2PacketSymbol::TYPE_SYNC:
			return "SYNC";
		case USB2PacketSymbol::TYPE_EOP:
			return "EOP";
		case USB2PacketSymbol::TYPE_RESET:
			return "RESET";
		case USB2PacketSymbol::TYPE_DATA:
		{
			char tmp[16];
			snprintf(tmp, sizeof(tmp), "%02x", sample.m_sample.m_data);
			return string(tmp);
		}
		case USB2PacketSymbol::TYPE_ERROR:
		default:
			return "ERROR";
	}

	return "";
}
