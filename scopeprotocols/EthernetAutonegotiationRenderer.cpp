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
	@brief Implementation of EthernetAutonegotiationRenderer
 */

#include "../scopehal/scopehal.h"
#include "../scopehal/ChannelRenderer.h"
#include "../scopehal/TextRenderer.h"
#include "EthernetAutonegotiationRenderer.h"
#include "EthernetAutonegotiationDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

EthernetAutonegotiationRenderer::EthernetAutonegotiationRenderer(OscilloscopeChannel* channel)
	: TextRenderer(channel)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rendering

Gdk::Color EthernetAutonegotiationRenderer::GetColor(int i)
{
	return TextRenderer::GetColor(i);
}

string EthernetAutonegotiationRenderer::GetText(int i)
{
	EthernetAutonegotiationCapture* data = dynamic_cast<EthernetAutonegotiationCapture*>(m_channel->GetData());
	if(data == NULL)
		return "";
	if(i >= (int)data->m_samples.size())
		return "";

	auto s = data->m_samples[i];
	unsigned int sel = s & 0x1f;
	unsigned int ability = (s >> 5) & 0x7f;
	bool xnp = (s >> 12) & 1;
	bool rf = (s >> 13) & 1;
	bool ack = (s >> 14) & 1;
	bool np = (s >> 15) & 1;

	//Not 802.3? Just display as hex
	char tmp[128];
	if(sel != 1)
	{
		snprintf(tmp, sizeof(tmp), "%04x", (int)s);
		return tmp;
	}

	//Yes, it's 802.3
	string ret = "Base: ";
	if(ability & 0x40)
		ret += "apause ";
	if(ability & 0x20)
		ret += "pause ";
	if(ability & 0x10)
		ret += "T4 ";
	if(ability & 0x8)
		ret += "100/full ";
	if(ability & 0x4)
		ret += "100/half ";
	if(ability & 0x2)
		ret += "10/full ";
	if(ability & 0x1)
		ret += "10/half ";

	if(xnp)
		ret += "XNP ";
	if(rf)
		ret += "FAULT ";
	if(ack)
		ret += "ACK ";
	if(np)
		ret += "Next-page";

	return ret;
}
