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
#include "USB2PMADecoder.h"

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
		return m_standardColors[COLOR_ERROR];
	if(i >= (int)data->m_samples.size())
		return m_standardColors[COLOR_ERROR];

	auto sample = data->m_samples[i];
	switch(sample.m_sample.m_type)
	{
		case USB2PacketSymbol::TYPE_PID:
			if( (sample.m_sample.m_data == USB2PacketSymbol::PID_RESERVED) ||
				(sample.m_sample.m_data == USB2PacketSymbol::PID_STALL) )
				return m_standardColors[COLOR_ERROR];
			else
				return m_standardColors[COLOR_PREAMBLE];

		case USB2PacketSymbol::TYPE_ADDR:
			return m_standardColors[COLOR_ADDRESS];

		case USB2PacketSymbol::TYPE_ENDP:
			return m_standardColors[COLOR_ADDRESS];

		case USB2PacketSymbol::TYPE_NFRAME:
			return m_standardColors[COLOR_DATA];

		case USB2PacketSymbol::TYPE_CRC5:
		case USB2PacketSymbol::TYPE_CRC16:
			return m_standardColors[COLOR_CHECKSUM_OK];	//TODO: verify checksum

		case USB2PacketSymbol::TYPE_DATA:
			return m_standardColors[COLOR_DATA];

		//invalid state, should never happen
		case USB2PacketSymbol::TYPE_ERROR:
		default:
			return m_standardColors[COLOR_ERROR];
	}
}

string USB2PacketRenderer::GetText(int i)
{
	USB2PacketCapture* data = dynamic_cast<USB2PacketCapture*>(m_channel->GetData());
	if(data == NULL)
		return "";
	if(i >= (int)data->m_samples.size())
		return "";

	char tmp[32];

	auto sample = data->m_samples[i];
	switch(sample.m_sample.m_type)
	{
		case USB2PacketSymbol::TYPE_PID:
		{
			switch(sample.m_sample.m_data & 0x0f)
			{
				case USB2PacketSymbol::PID_RESERVED:
					return "RESERVED";
				case USB2PacketSymbol::PID_OUT:
					return "OUT";
				case USB2PacketSymbol::PID_ACK:
					return "ACK";
				case USB2PacketSymbol::PID_DATA0:
					return "DATA0";
				case USB2PacketSymbol::PID_PING:
					return "PING";
				case USB2PacketSymbol::PID_SOF:
					return "SOF";
				case USB2PacketSymbol::PID_NYET:
					return "NYET";
				case USB2PacketSymbol::PID_DATA2:
					return "DATA2";
				case USB2PacketSymbol::PID_SPLIT:
					return "SPLIT";
				case USB2PacketSymbol::PID_IN:
					return "IN";
				case USB2PacketSymbol::PID_NAK:
					return "NAK";
				case USB2PacketSymbol::PID_DATA1:
					return "DATA1";
				case USB2PacketSymbol::PID_PRE_ERR:
					return "PRE/ERR";
				case USB2PacketSymbol::PID_SETUP:
					return "SETUP";
				case USB2PacketSymbol::PID_STALL:
					return "STALL";
				case USB2PacketSymbol::PID_MDATA:
					return "MDATA";

				default:
					return "INVALID PID";
			}
			break;
		}
		case USB2PacketSymbol::TYPE_ADDR:
			snprintf(tmp, sizeof(tmp), "Dev %d", sample.m_sample.m_data);
			return string(tmp);
		case USB2PacketSymbol::TYPE_NFRAME:
			snprintf(tmp, sizeof(tmp), "Frame %d", sample.m_sample.m_data);
			return string(tmp);
		case USB2PacketSymbol::TYPE_ENDP:
			snprintf(tmp, sizeof(tmp), "EP %d", sample.m_sample.m_data);
			return string(tmp);
		case USB2PacketSymbol::TYPE_CRC5:
			snprintf(tmp, sizeof(tmp), "CRC %02x", sample.m_sample.m_data);
			return string(tmp);
		case USB2PacketSymbol::TYPE_CRC16:
			snprintf(tmp, sizeof(tmp), "CRC %04x", sample.m_sample.m_data);
			return string(tmp);
		case USB2PacketSymbol::TYPE_DATA:
			snprintf(tmp, sizeof(tmp), "%02x", sample.m_sample.m_data);
			return string(tmp);
		case USB2PacketSymbol::TYPE_ERROR:
		default:
			return "ERROR";
	}

	return "";
}
