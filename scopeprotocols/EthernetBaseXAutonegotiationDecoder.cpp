/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2023 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Implementation of EthernetBaseXAutonegotiationDecoder
 */

#include "../scopehal/scopehal.h"
#include "IBM8b10bDecoder.h"
#include "EthernetBaseXAutonegotiationDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

EthernetBaseXAutonegotiationDecoder::EthernetBaseXAutonegotiationDecoder(const string& color)
	: PacketDecoder(color, CAT_SERIAL)
{
	CreateInput("din");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool EthernetBaseXAutonegotiationDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (dynamic_cast<IBM8b10bWaveform*>(stream.GetData()) != nullptr) )
		return true;

	return false;
}

string EthernetBaseXAutonegotiationDecoder::GetProtocolName()
{
	return "Ethernet Base-X Autonegotiation";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void EthernetBaseXAutonegotiationDecoder::Refresh()
{
	ClearPackets();

	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din = dynamic_cast<IBM8b10bWaveform*>(GetInputWaveform(0));
	if(!din)
		return;
	din->PrepareForCpuAccess();

	//Create the outbound data
	auto* cap = new EthernetBaseXAutonegotiationWaveform;
	cap->m_timescale = din->m_timescale;
	cap->m_triggerPhase = din->m_triggerPhase;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	cap->PrepareForCpuAccess();

	enum
	{
		STATE_IDLE,
		STATE_HEADER,
		STATE_FIRST,
		STATE_SECOND
	} state = STATE_IDLE;

	//Crunch it
	auto len = din->size();
	int64_t tstart = 0;
	uint8_t low = 0;
	for(size_t i = 0; i < len; i ++)
	{
		auto tnow = din->m_offsets[i];
		switch(state)
		{
			case STATE_IDLE:
				{
					//Comma? Might be start of an idle
					if(din->m_samples[i].m_control && (din->m_samples[i].m_data == 0xbc))
					{
						tstart = tnow;
						state = STATE_HEADER;
					}
				}
				break;	//end STATE_IDLE

			case STATE_HEADER:
				{
					//Should be D2.2 0x42 (for C2) or D21.5 0xb5 (for C1)
					if(!din->m_samples[i].m_control &&
						( (din->m_samples[i].m_data == 0x42) || (din->m_samples[i].m_data == 0xb5) ))
					{
						state = STATE_FIRST;
					}
					else
						state = STATE_IDLE;
				}
				break;	//end STATE_HEADER

			case STATE_FIRST:
				{
					if(!din->m_samples[i].m_control)
					{
						//Low half of ability field
						low = din->m_samples[i].m_data;
						state = STATE_SECOND;
					}
					else
						state = STATE_IDLE;
				}
				break;	//end STATE_FIRST

			case STATE_SECOND:
				{
					if(!din->m_samples[i].m_control)
					{
						uint16_t code = low | (din->m_samples[i].m_data << 8);

						if(code & 1)
						{
							cap->m_samples.push_back(EthernetBaseXAutonegotiationSample(
								EthernetBaseXAutonegotiationSample::TYPE_SGMII, code));
						}
						else
						{
							cap->m_samples.push_back(EthernetBaseXAutonegotiationSample(
								EthernetBaseXAutonegotiationSample::TYPE_BASE_PAGE, code));
						}
						cap->m_offsets.push_back(tstart);
						cap->m_durations.push_back(din->m_durations[i] + tnow - tstart);

						/*
						auto pack = new Packet;
						pack->m_headers["Type"] = "Base";
						pack->m_headers["Ack"] = (code & ACK) ? "1" : "0";
						pack->m_headers["Info"] = cap->GetText(cap->m_samples.size()-1);
						pack->m_headers["T"] = (code & TOGGLE) ? "1" : "0";
						pack->m_headers["NP"] = (code & NP) ? "1" : "0";
						pack->m_data.push_back(code >> 8);
						pack->m_data.push_back(code & 0xff);
						pack->m_offset = tnow * din->m_timescale + din->m_triggerPhase;
						pack->m_len = din->m_durations[i] * din->m_timescale;
						pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
						m_packets.push_back(pack);
						*/
					}

					state = STATE_IDLE;
				}
				break;

			default:
				break;
		}
	}

	SetData(cap, 0);
	cap->MarkModifiedFromCpu();
}

string EthernetBaseXAutonegotiationWaveform::GetColor(size_t i)
{
	auto s = m_samples[i];
	switch(s.m_type)
	{
		case EthernetBaseXAutonegotiationSample::TYPE_BASE_PAGE:
			return StandardColors::colors[StandardColors::COLOR_DATA];

		case EthernetBaseXAutonegotiationSample::TYPE_SGMII:
			return StandardColors::colors[StandardColors::COLOR_CONTROL];

		default:
			return StandardColors::colors[StandardColors::COLOR_ERROR];

	}
}

string EthernetBaseXAutonegotiationWaveform::GetText(size_t i)
{
	string ret;

	auto s = m_samples[i];
	char tmp[128];
	switch(s.m_type)
	{
		case EthernetBaseXAutonegotiationSample::TYPE_BASE_PAGE:
			{
				if(s.m_value & 0x8000)
					ret += "NP ";
				if(s.m_value & 0x4000)
					ret += "ACK ";
				if(s.m_value & 0x0020)
					ret += "Full ";
				if(s.m_value & 0x0040)
					ret += "Half ";
				switch( (s.m_value >> 7) & 3)
				{
					case 0:
						break;
					case 1:
						ret += "AsymPause ";
						break;
					case 2:
						ret += "SymPause ";
						break;
					case 3:
						ret += "SymAsymPause ";
						break;
				}
				switch( (s.m_value >> 12) & 3)
				{
					case 0:
						break;
					case 1:
						ret += "Offline ";
						break;
					case 2:
						ret += "LinkFail ";
						break;
					case 3:
						ret += "AnegFail ";
						break;
				}

				if(ret == "")
					return "Empty";
			}
			return ret;

		case EthernetBaseXAutonegotiationSample::TYPE_SGMII:
			{
				if(s.m_value & 0x8000)
					ret += "Up ";
				else
					ret += "Down ";
				switch( (s.m_value >> 10) & 3)
				{
					case 0:
						ret += "10/";
						break;
					case 1:
						ret += "100/";
						break;
					case 2:
						ret += "1000/";
						break;
				}
				if(s.m_value & 0x1000)
					ret += "Full ";
				else
					ret += "Half ";

				if(ret == "")
					return "Empty";
			}
			return ret;

		default:
			snprintf(tmp, sizeof(tmp), "Invalid (%04x)", (int)s.m_value);
			return tmp;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Packet decoding

vector<string> EthernetBaseXAutonegotiationDecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("Type");
	ret.push_back("Ack");
	ret.push_back("T");
	ret.push_back("Ack2");
	ret.push_back("NP");
	ret.push_back("Info");
	return ret;
}

bool EthernetBaseXAutonegotiationDecoder::CanMerge(Packet* first, Packet* /*cur*/, Packet* next)
{
	//Merge base page with subsequent base pages (and their acks)
	if( (first->m_headers["Type"] == "Base") && (next->m_headers["Type"] == "Base") )
		return true;

	//Merge message page with subsequent ACKs and unformatted pages
	if(first->m_headers["Type"] == "Message")
	{
		if( (next->m_headers["Type"] == "Message") &&
			( (next->m_headers["Info"] == "ACK") || (next->m_headers["Info"] == first->m_headers["Info"]) ) )
		{
			return true;
		}

		if(next->m_headers["Type"] == "Unformatted")
			return true;
	}

	return false;
}

Packet* EthernetBaseXAutonegotiationDecoder::CreateMergedHeader(Packet* pack, size_t i)
{
	//Default to copying everything
	Packet* ret = new Packet;
	ret->m_offset = pack->m_offset;
	ret->m_len = pack->m_len;
	ret->m_headers = pack->m_headers;
	ret->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];

	if(pack->m_headers["Type"] == "Base")
	{
		//Extend lengths
		for(; i<m_packets.size(); i++)
		{
			if(CanMerge(pack, nullptr, m_packets[i]))
				ret->m_len = (m_packets[i]->m_offset + m_packets[i]->m_len) - pack->m_offset;
			else
				break;
		}
	}

	if(pack->m_headers["Type"] == "Message")
	{
		ret->m_headers["Type"] = pack->m_headers["Info"];
		ret->m_headers["Info"] = "";

		string lastT = pack->m_headers["T"];

		//Check subsequent packets for unformatted pages that might be interesting
		for(; i<m_packets.size(); i++)
		{
			auto p = m_packets[i];

			if(CanMerge(pack, nullptr, p))
			{
				//Only care if it's a new toggle
				auto curT = p->m_headers["T"];
				if( (curT != lastT) && (p->m_headers["Type"] == "Unformatted") )
				{
					ret->m_headers["Info"] += p->m_headers["Info"] + " ";
					lastT = curT;
				}

				ret->m_len = (p->m_offset + p->m_len) - pack->m_offset;
			}
			else
				break;
		}
	}

	return ret;
}
