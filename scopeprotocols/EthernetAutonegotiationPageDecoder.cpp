/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2022 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Implementation of EthernetAutonegotiationPageDecoder
 */

#include "../scopehal/scopehal.h"
#include "EthernetAutonegotiationDecoder.h"
#include "EthernetAutonegotiationPageDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

EthernetAutonegotiationPageDecoder::EthernetAutonegotiationPageDecoder(const string& color)
	: PacketDecoder(color, CAT_SERIAL)
{
	CreateInput("din");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool EthernetAutonegotiationPageDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (dynamic_cast<EthernetAutonegotiationWaveform*>(stream.GetData()) != nullptr) )
		return true;

	return false;
}

string EthernetAutonegotiationPageDecoder::GetProtocolName()
{
	return "Ethernet Autonegotiation Page";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void EthernetAutonegotiationPageDecoder::Refresh()
{
	ClearPackets();

	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din = dynamic_cast<EthernetAutonegotiationWaveform*>(GetInputWaveform(0));
	if(!din)
		return;
	din->PrepareForCpuAccess();

	//Create the outbound data
	auto* cap = new EthernetAutonegotiationPageWaveform;
	cap->m_timescale = din->m_timescale;
	cap->m_triggerPhase = din->m_triggerPhase;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	cap->PrepareForCpuAccess();

	enum
	{
		STATE_IDLE,
		STATE_BASE_PAGE,
		STATE_ACK,
		STATE_NEXT_PAGE
	} state = STATE_IDLE;

	const uint16_t ACK = 0x4000;
	const uint16_t ACK2 = 0x1000;
	const uint16_t ACKS = ACK | ACK2;
	const uint16_t MP = 0x2000;
	const uint16_t NP = 0x8000;
	const uint16_t TOGGLE = 0x800;

	int messageCount = 0;
	int lastMessage = 0;

	//Crunch it
	auto len = din->size();
	int64_t tstart = 0;
	uint16_t codeOrig = 0;
	string lastType;
	for(size_t i = 0; i < len; i ++)
	{
		uint16_t code = din->m_samples[i];
		auto tnow = din->m_offsets[i];
		switch(state)
		{
			//Expect the first codeword we see is a base page
			case STATE_IDLE:
				{
					//Base page?
					if((code & 0x1f) == 1)
					{
						state = STATE_BASE_PAGE;
						tstart = tnow;
						codeOrig = code;
						cap->m_samples.push_back(EthernetAutonegotiationPageSample(
							EthernetAutonegotiationPageSample::TYPE_BASE_PAGE, code));
						cap->m_offsets.push_back(tnow);
						cap->m_durations.push_back(din->m_durations[i]);

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
					}

					//Something else. Ignore it.
					else
					{
					}
				}
				break;

			//Continue base page
			case STATE_BASE_PAGE:

				//Look for an ACK
				if(code & ACK)
				{
					//Extend the previous sample up to but not including our new codeword
					cap->m_durations[cap->m_durations.size() - 1] = tnow - tstart;

					//Create the ACK symbol
					state = STATE_ACK;
					tstart = tnow;
					codeOrig = code;

					cap->m_samples.push_back(EthernetAutonegotiationPageSample(
						EthernetAutonegotiationPageSample::TYPE_ACK, code));
					cap->m_offsets.push_back(tnow);
					cap->m_durations.push_back(din->m_durations[i]);

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
					lastType = "Base";
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_STATUS];
					m_packets.push_back(pack);
				}

				//Same codeword? Extend it
				else if(code == codeOrig)
				{
					cap->m_durations[cap->m_durations.size() - 1] = (tnow + len) - tstart;

					//Add new packets for the original events
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
				}

				//else it's an error, ignore it?

				break;

			//Continue an ACK
			case STATE_ACK:

				//Extend the ACK
				if( (code & ACK) && ( (code & ~ACKS) == (codeOrig & ~ACKS) ) )
				{
					cap->m_durations[cap->m_durations.size() - 1] = (tnow + len) - tstart;

					auto pack = new Packet;
					pack->m_headers["Type"] = lastType;
					pack->m_headers["Ack"] = (code & ACK) ? "1" : "0";
					pack->m_headers["Info"] = cap->GetText(cap->m_samples.size()-1);
					pack->m_headers["T"] = (code & TOGGLE) ? "1" : "0";
					pack->m_headers["Ack2"] = (code & ACK2) ? "1" : "0";
					pack->m_headers["NP"] = (code & NP) ? "1" : "0";
					pack->m_data.push_back(code >> 8);
					pack->m_data.push_back(code & 0xff);
					pack->m_offset = tnow * din->m_timescale + din->m_triggerPhase;
					pack->m_len = din->m_durations[i] * din->m_timescale;
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_STATUS];
					m_packets.push_back(pack);
				}

				//else start a new codeword
				//TODO: we should probably check the toggle bit
				else
				{
					//Extend the previous sample up to but not including our new codeword
					cap->m_durations[cap->m_durations.size() - 1] = tnow - tstart;

					//Prepare to add the new sample
					cap->m_offsets.push_back(tnow);
					cap->m_durations.push_back(din->m_durations[i]);

					//Message page?
					if(code & MP)
					{
						state = STATE_NEXT_PAGE;
						cap->m_samples.push_back(EthernetAutonegotiationPageSample(
							EthernetAutonegotiationPageSample::TYPE_MESSAGE_PAGE, code));

						auto pack = new Packet;
						pack->m_headers["Type"] = "Message";
						lastType = pack->m_headers["Type"];
						pack->m_headers["Ack"] = (code & ACK) ? "1" : "0";
						pack->m_headers["Info"] = cap->GetText(cap->m_samples.size()-1);
						pack->m_headers["T"] = (code & TOGGLE) ? "1" : "0";
						pack->m_headers["Ack2"] = (code & ACK2) ? "1" : "0";
						pack->m_headers["NP"] = (code & NP) ? "1" : "0";
						pack->m_data.push_back(code >> 8);
						pack->m_data.push_back(code & 0xff);
						pack->m_offset = tnow * din->m_timescale + din->m_triggerPhase;
						pack->m_len = din->m_durations[i] * din->m_timescale;
						pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_CONTROL];
						lastType = pack->m_headers["Type"];
						m_packets.push_back(pack);

						messageCount = 0;
						lastMessage = (code & 0x7ff);
					}

					//No, unformatted page
					else
					{
						state = STATE_NEXT_PAGE;

						//Handle known message types
						switch(lastMessage)
						{
							case 8:
								if(messageCount == 0)
								{
									cap->m_samples.push_back(EthernetAutonegotiationPageSample(
										EthernetAutonegotiationPageSample::TYPE_1000BASET_TECH_0, code));
								}
								else if(messageCount == 1)
								{
									cap->m_samples.push_back(EthernetAutonegotiationPageSample(
										EthernetAutonegotiationPageSample::TYPE_1000BASET_TECH_1, code));
								}
								else
								{
									cap->m_samples.push_back(EthernetAutonegotiationPageSample(
										EthernetAutonegotiationPageSample::TYPE_UNFORMATTED_PAGE, code));
								}
								break;

							case 10:
								if(messageCount == 0)
								{
									cap->m_samples.push_back(EthernetAutonegotiationPageSample(
										EthernetAutonegotiationPageSample::TYPE_EEE_TECH, code));
								}
								else
								{
									cap->m_samples.push_back(EthernetAutonegotiationPageSample(
										EthernetAutonegotiationPageSample::TYPE_UNFORMATTED_PAGE, code));
								}
								break;

							//Generic unformatted page
							default:
								cap->m_samples.push_back(EthernetAutonegotiationPageSample(
									EthernetAutonegotiationPageSample::TYPE_UNFORMATTED_PAGE, code));
								break;
						}

						auto pack = new Packet;
						pack->m_headers["Type"] = "Unformatted";
						lastType = pack->m_headers["Type"];
						pack->m_headers["Ack"] = (code & ACK) ? "1" : "0";
						pack->m_headers["Info"] = cap->GetText(cap->m_samples.size()-1);
						pack->m_headers["T"] = (code & TOGGLE) ? "1" : "0";
						pack->m_headers["Ack2"] = (code & ACK2) ? "1" : "0";
						pack->m_headers["NP"] = (code & NP) ? "1" : "0";
						pack->m_data.push_back(code >> 8);
						pack->m_data.push_back(code & 0xff);
						pack->m_offset = tnow * din->m_timescale + din->m_triggerPhase;
						pack->m_len = din->m_durations[i] * din->m_timescale;
						pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
						m_packets.push_back(pack);

						messageCount ++;
					}

					tstart = tnow;
					codeOrig = code;
				}

				break;

			//Process a Next Page
			case STATE_NEXT_PAGE:

				//Look for an ACK
				if(code & ACK)
				{
					//Extend the previous sample up to but not including our new codeword
					cap->m_durations[cap->m_durations.size() - 1] = tnow - tstart;

					//Create the ACK symbol
					state = STATE_ACK;
					tstart = tnow;
					codeOrig = code;

					cap->m_samples.push_back(EthernetAutonegotiationPageSample(
						EthernetAutonegotiationPageSample::TYPE_ACK, code));
					cap->m_offsets.push_back(tnow);
					cap->m_durations.push_back(din->m_durations[i]);

					auto pack = new Packet;
					pack->m_headers["Type"] = lastType;
					pack->m_headers["Ack"] = (code & ACK) ? "1" : "0";
					pack->m_headers["Info"] = cap->GetText(cap->m_samples.size()-1);
					pack->m_headers["T"] = (code & TOGGLE) ? "1" : "0";
					pack->m_headers["Ack2"] = (code & ACK2) ? "1" : "0";
					pack->m_headers["NP"] = (code & NP) ? "1" : "0";
					pack->m_data.push_back(code >> 8);
					pack->m_data.push_back(code & 0xff);
					pack->m_offset = tnow * din->m_timescale + din->m_triggerPhase;
					pack->m_len = din->m_durations[i] * din->m_timescale;
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_STATUS];
					m_packets.push_back(pack);
				}

				//Same codeword? Extend it
				else if(code == codeOrig)
				{
					cap->m_durations[cap->m_durations.size() - 1] = (tnow + len) - tstart;

					auto pack = new Packet;
					pack->m_headers["Type"] = lastType;
					pack->m_headers["Ack"] = (code & ACK) ? "1" : "0";
					pack->m_headers["Info"] = cap->GetText(cap->m_samples.size()-1);
					pack->m_headers["T"] = (code & TOGGLE) ? "1" : "0";
					pack->m_headers["Ack2"] = (code & ACK2) ? "1" : "0";
					pack->m_headers["NP"] = (code & NP) ? "1" : "0";
					pack->m_data.push_back(code >> 8);
					pack->m_data.push_back(code & 0xff);
					pack->m_offset = tnow * din->m_timescale + din->m_triggerPhase;
					pack->m_len = din->m_durations[i] * din->m_timescale;
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
					lastType = pack->m_headers["Type"];
					m_packets.push_back(pack);
				}

				//else it's an error, ignore it?

				break;

			default:
				break;
		}
	}

	SetData(cap, 0);
	cap->MarkModifiedFromCpu();
}

string EthernetAutonegotiationPageWaveform::GetColor(size_t i)
{
	auto s = m_samples[i];
	switch(s.m_type)
	{
		case EthernetAutonegotiationPageSample::TYPE_BASE_PAGE:
		case EthernetAutonegotiationPageSample::TYPE_1000BASET_TECH_0:
		case EthernetAutonegotiationPageSample::TYPE_1000BASET_TECH_1:
		case EthernetAutonegotiationPageSample::TYPE_UNFORMATTED_PAGE:
		case EthernetAutonegotiationPageSample::TYPE_EEE_TECH:
			return StandardColors::colors[StandardColors::COLOR_DATA];

		case EthernetAutonegotiationPageSample::TYPE_MESSAGE_PAGE:
			return StandardColors::colors[StandardColors::COLOR_ADDRESS];

		case EthernetAutonegotiationPageSample::TYPE_ACK:
			return StandardColors::colors[StandardColors::COLOR_PREAMBLE];

		default:
			return StandardColors::colors[StandardColors::COLOR_ERROR];

	}
}

string EthernetAutonegotiationPageWaveform::GetText(size_t i)
{
	auto s = m_samples[i];
	char tmp[128];
	switch(s.m_type)
	{
		case EthernetAutonegotiationPageSample::TYPE_BASE_PAGE:
			{
				unsigned int sel = s.m_value & 0x1f;
				unsigned int ability = (s.m_value >> 5) & 0x7f;
				bool xnp = (s.m_value >> 12) & 1;
				bool rf = (s.m_value >> 13) & 1;
				bool ack = (s.m_value >> 14) & 1;
				bool np = (s.m_value >> 15) & 1;

				if(sel != 1)
					return "Invalid base page (not 802.3)";

				//Yes, it's 802.3
				string ret;
				if(ability & 0x40)
					ret += "apause ";
				if(ability & 0x20)
					ret += "pause ";
				if(ability & 0x10)
					ret += "T4 ";
				if(ability & 0xc)
				{
					ret += "100/";
					if( (ability & 0xc) == 0xc)
						ret += "full+half ";
					else if(ability & 0x8)
						ret += "full ";
					else if(ability & 0x4)
						ret += "half ";
				}
				if(ability & 0x3)
				{
					ret += "10/";
					if( (ability & 0x3) == 0x3)
						ret += "full+half ";
					else if(ability & 0x2)
						ret += "full ";
					else if(ability & 0x1)
						ret += "half ";
				}

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
			break;

		case EthernetAutonegotiationPageSample::TYPE_MESSAGE_PAGE:

			//802.3-2018 Annex 28C
			switch(s.m_value & 0x7ff)
			{
				case 0:
					return "Reserved";
				case 1:
					return "Null";
				case 2:
					return "Technology Ability (1)";
				case 3:
					return "Technology Ability (2)";
				case 4:
					return "Remote Fault";
				case 5:
					return "OUI Tagged";
				case 6:
					return "PHY ID";
				case 7:
					return "100Base-T2 Technology";
				case 8:
					return "1000Base-T Technology";
				case 9:
					return "MultiGBase-T Technology";
				case 10:
					return "EEE Technology";
				case 11:
					return "OUI Tagged";
				default:
					return "Reserved";
			}

			break;

		//802.3-2018 table 40-4
		case EthernetAutonegotiationPageSample::TYPE_1000BASET_TECH_0:
			{
				string ret;
				if(s.m_value & 0x10)
					ret += "1000baseT/half ";

				if(s.m_value & 0x8)
					ret += "1000baseT/full ";

				if(s.m_value & 0x4)
					ret += "Multiport ";
				else
					ret += "Single-port ";

				if(s.m_value & 0x1)
				{
					ret += "Manual: ";
					if(s.m_value & 2)
						ret += "Master";
					else
						ret += "Slave";
				}
				return ret;
			}
			break;

		//802.3-2018 table 40-4
		case EthernetAutonegotiationPageSample::TYPE_1000BASET_TECH_1:
			snprintf(tmp, sizeof(tmp), "Seed %03x", s.m_value & 0x7ff);
			return tmp;

		//802.3-2018 table 40-4 and 45.2.7.13
		case EthernetAutonegotiationPageSample::TYPE_EEE_TECH:
			{
				string ret;

				if(s.m_value & 0x4000)
					ret += "25GBase-R ";
				if(s.m_value & 0x2000)
					ret += "100GBase-CR4 ";
				if(s.m_value & 0x1000)
					ret += "100GBase-KR4 ";
				if(s.m_value & 0x800)
					ret += "100GBase-KP4 ";
				if(s.m_value & 0x400)
					ret += "100GBase-CR10 ";
				if(s.m_value & 0x200)
					ret += "40GBase-T ";
				if(s.m_value & 0x100)
					ret += "40GBase-CR4 ";
				if(s.m_value & 0x80)
					ret += "40GBase-KR4 ";
				if(s.m_value & 0x40)
					ret += "10GBase-KR ";
				if(s.m_value & 0x20)
					ret += "1GBase-KX4 ";
				if(s.m_value & 0x10)
					ret += "1000base-KX ";
				if(s.m_value & 8)
					ret += "10Gbase-T ";
				if(s.m_value & 4)
					ret += "1000base-T ";
				if(s.m_value & 2)
					ret += "100base-TX ";
				if(s.m_value & 1)
					ret += "25GBase-T ";

				if(ret.empty())
					return "No EEE support";
				else
					return ret;
			}
			break;

		case EthernetAutonegotiationPageSample::TYPE_UNFORMATTED_PAGE:
			snprintf(tmp, sizeof(tmp), "%04x", (int)s.m_value);
			return tmp;

		case EthernetAutonegotiationPageSample::TYPE_ACK:
			return "ACK";

		default:
			snprintf(tmp, sizeof(tmp), "Invalid (%04x)", (int)s.m_value);
			return tmp;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Packet decoding

vector<string> EthernetAutonegotiationPageDecoder::GetHeaders()
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

bool EthernetAutonegotiationPageDecoder::CanMerge(Packet* first, Packet* /*cur*/, Packet* next)
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

Packet* EthernetAutonegotiationPageDecoder::CreateMergedHeader(Packet* pack, size_t i)
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
