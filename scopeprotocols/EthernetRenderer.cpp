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
	@brief Implementation of EthernetRenderer
 */

#include "../scopehal/scopehal.h"
#include "../scopehal/ChannelRenderer.h"
#include "../scopehal/TextRenderer.h"
#include "EthernetRenderer.h"
#include "EthernetProtocolDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

EthernetRenderer::EthernetRenderer(OscilloscopeChannel* channel)
	: TextRenderer(channel)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rendering

Gdk::Color EthernetRenderer::GetColor(int i)
{
	EthernetCapture* data = dynamic_cast<EthernetCapture*>(m_channel->GetData());
	if(data == NULL)
		return Gdk::Color("#000000");
	if(i >= (int)data->m_samples.size())
		return Gdk::Color("#000000");

	//TODO: have a set of standard colors we use everywhere?

	auto sample = data->m_samples[i];
	switch(sample.m_sample.m_type)
	{
		//Preamble: gray (not interesting)
		case EthernetFrameSegment::TYPE_PREAMBLE:
			return Gdk::Color("#808080");

		//SFD: yellow
		case EthernetFrameSegment::TYPE_SFD:
			return Gdk::Color("#ffff80");

		//MAC addresses (src or dest): cyan
		case EthernetFrameSegment::TYPE_DST_MAC:
		case EthernetFrameSegment::TYPE_SRC_MAC:
			return Gdk::Color("#80ffff");

		//Ethertype: Pink
		case EthernetFrameSegment::TYPE_ETHERTYPE:
		case EthernetFrameSegment::TYPE_VLAN_TAG:
			return Gdk::Color("#ffcccc");

		//Checksum: Green or red depending on if it's correct or not
		//For now, always green b/c we don't implement the FCS :D
		case EthernetFrameSegment::TYPE_FCS:
			return Gdk::Color("#00ff00");

		//Payload: dark blue
		default:
			return Gdk::Color("#336699");
	}
}

string EthernetRenderer::GetText(int i)
{
	EthernetCapture* data = dynamic_cast<EthernetCapture*>(m_channel->GetData());
	if(data == NULL)
		return "";
	if(i >= (int)data->m_samples.size())
		return "";

	auto sample = data->m_samples[i];
	switch(sample.m_sample.m_type)
	{
		case EthernetFrameSegment::TYPE_PREAMBLE:
			return "PREAMBLE";

		case EthernetFrameSegment::TYPE_SFD:
			return "SFD";

		case EthernetFrameSegment::TYPE_DST_MAC:
			{
				if(sample.m_sample.m_data.size() != 6)
					return "[invalid dest MAC length]";

				char tmp[32];
				snprintf(tmp, sizeof(tmp), "Dest MAC: %02x:%02x:%02x:%02x:%02x:%02x",
					sample.m_sample.m_data[0],
					sample.m_sample.m_data[1],
					sample.m_sample.m_data[2],
					sample.m_sample.m_data[3],
					sample.m_sample.m_data[4],
					sample.m_sample.m_data[5]);
				return tmp;
			}

		case EthernetFrameSegment::TYPE_SRC_MAC:
			{
				if(sample.m_sample.m_data.size() != 6)
					return "[invalid src MAC length]";

				char tmp[32];
				snprintf(tmp, sizeof(tmp), "Src MAC: %02x:%02x:%02x:%02x:%02x:%02x",
					sample.m_sample.m_data[0],
					sample.m_sample.m_data[1],
					sample.m_sample.m_data[2],
					sample.m_sample.m_data[3],
					sample.m_sample.m_data[4],
					sample.m_sample.m_data[5]);
				return tmp;
			}

		case EthernetFrameSegment::TYPE_ETHERTYPE:
			{
				if(sample.m_sample.m_data.size() != 2)
					return "[invalid Ethertype length]";

				string type = "Type: ";

				char tmp[32];
				uint16_t ethertype = (sample.m_sample.m_data[0] << 8) | sample.m_sample.m_data[1];
				switch(ethertype)
				{
					case 0x0800:
						type += "IPv4";
						break;

					case 0x0806:
						type += "ARP";
						break;

					case 0x8100:
						type += "802.1q";
						break;

					case 0x86dd:
						type += "IPv6";
						break;

					case 0x88cc:
						type += "LLDP";
						break;

					case 0x88f7:
						type += "PTP";
						break;

					default:
						snprintf(tmp, sizeof(tmp), "0x%04x", ethertype);
						type += tmp;
						break;
				}

				//TODO: look up a table of common Ethertype values

				return type;
			}

		case EthernetFrameSegment::TYPE_PAYLOAD:
			{
				string ret;
				for(auto b : sample.m_sample.m_data)
				{
					char tmp[32];
					snprintf(tmp, sizeof(tmp), "%02x ", b);
					ret += tmp;
				}
				return ret;
			}

		case EthernetFrameSegment::TYPE_FCS:
			{
				if(sample.m_sample.m_data.size() != 4)
					return "[invalid FCS length]";

				char tmp[32];
				snprintf(tmp, sizeof(tmp), "CRC: %02x%02x%02x%02x",
					sample.m_sample.m_data[0],
					sample.m_sample.m_data[1],
					sample.m_sample.m_data[2],
					sample.m_sample.m_data[3]);
				return tmp;
			}

		default:
			break;
	}

	return "";
}
