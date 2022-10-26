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

#include "../scopehal/scopehal.h"
#include "IPv4Decoder.h"
#include "EthernetProtocolDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

IPv4Decoder::IPv4Decoder(const string& color)
	: Filter(color, CAT_SERIAL)
{
	AddProtocolStream("data");
	CreateInput("eth");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool IPv4Decoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (dynamic_cast<EthernetWaveform*>(stream.m_channel->GetData(0)) != NULL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string IPv4Decoder::GetProtocolName()
{
	return "IPv4";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void IPv4Decoder::Refresh()
{
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din = dynamic_cast<EthernetWaveform*>(GetInputWaveform(0));
	din->PrepareForCpuAccess();
	size_t len = din->m_samples.size();

	//Loop over the events and process stuff
	auto cap = new IPv4Waveform;
	cap->PrepareForCpuAccess();
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;

	int state = 0;
	int header_len = 0;
	for(size_t i=0; i<len; i++)
	{
		auto s = din->m_samples[i];
		int64_t halfdur = din->m_durations[i]/2;

		switch(state)
		{
			//Wait for SFD. Ignore any errors, preambles, etc before this
			case 0:
				if(s.m_type == EthernetFrameSegment::TYPE_SFD)
					state = 1;
				break;

			//Next should be dest MAC. Ignore it
			case 1:
				if(s.m_type == EthernetFrameSegment::TYPE_DST_MAC)
					state = 2;
				else
					state = 0;
				break;

			//Then source MAC
			case 2:
				if(s.m_type == EthernetFrameSegment::TYPE_SRC_MAC)
					state = 3;
				else
					state = 0;
				break;

			//Next is ethertype. Could be 802.1q or IPv4.
			case 3:
				if(s.m_type == EthernetFrameSegment::TYPE_ETHERTYPE)
				{
					uint16_t ethertype = (s.m_data[0] << 8) | s.m_data[1];

					//802.1q tag
					if(ethertype == 0x8100)
						state = 4;

					//IPv4
					else if(ethertype == 0x0800)
						state = 5;

					//Something else, discard the packet as uninteresting
					else
						state = 0;
				}
				else
					state = 0;

				break;

			//802.1q frame? Expect a VLAN tag, then look for the real ethertyoe
			case 4:
				if(s.m_type == EthernetFrameSegment::TYPE_VLAN_TAG)
					state = 3;
				else
					state = 0;
				break;

			//Should be IP version and header length
			case 5:
				if(s.m_type == EthernetFrameSegment::TYPE_PAYLOAD)
				{
					uint8_t data = s.m_data[0];

					//Expect 0x4-something for IP version
					if( (data >> 4) == 4)
					{
						cap->m_offsets.push_back(din->m_offsets[i]);
						cap->m_durations.push_back(halfdur);
						cap->m_samples.push_back(IPv4Symbol(IPv4Symbol::TYPE_VERSION, 4));
					}
					else
					{
						state = 0;
						break;
					}

					//Header length
					header_len = data & 0xf;

					cap->m_offsets.push_back(din->m_offsets[i] + halfdur);
					cap->m_durations.push_back(halfdur);
					cap->m_samples.push_back(IPv4Symbol(IPv4Symbol::TYPE_HEADER_LEN, header_len));

					state = 6;
				}
				else
					state = 0;

				break;

			//Diffserv code point and ECN
			case 6:
				if(s.m_type == EthernetFrameSegment::TYPE_PAYLOAD)
				{
					cap->m_offsets.push_back(din->m_offsets[i]);
					cap->m_durations.push_back(din->m_durations[i]);
					cap->m_samples.push_back(IPv4Symbol(IPv4Symbol::TYPE_DIFFSERV, s.m_data[0]));
					state = 7;
				}
				else
					state = 0;
				break;

			//Total length
			case 7:
				if(s.m_type == EthernetFrameSegment::TYPE_PAYLOAD)
				{
					cap->m_offsets.push_back(din->m_offsets[i]);
					cap->m_durations.push_back(din->m_durations[i]);
					cap->m_samples.push_back(IPv4Symbol(IPv4Symbol::TYPE_LENGTH, s.m_data[0]));
					state = 8;
				}
				else
					state = 0;
				break;
			case 8:
				if(s.m_type == EthernetFrameSegment::TYPE_PAYLOAD)
				{
					//Append to the previous sample
					size_t n = cap->m_offsets.size() - 1;
					cap->m_durations[n] = din->m_offsets[i] + din->m_durations[i] - cap->m_offsets[n];
					cap->m_samples[n].m_data.push_back(s.m_data[0]);
					state = 9;
				}
				else
					state = 0;
				break;

			//Identification
			case 9:
				if(s.m_type == EthernetFrameSegment::TYPE_PAYLOAD)
				{
					cap->m_offsets.push_back(din->m_offsets[i]);
					cap->m_durations.push_back(din->m_durations[i]);
					cap->m_samples.push_back(IPv4Symbol(IPv4Symbol::TYPE_ID, s.m_data[0]));
					state = 10;
				}
				else
					state = 0;
				break;
			case 10:
				if(s.m_type == EthernetFrameSegment::TYPE_PAYLOAD)
				{
					//Append to the previous sample
					size_t n = cap->m_offsets.size() - 1;
					cap->m_durations[n] = din->m_offsets[i] + din->m_durations[i] - cap->m_offsets[n];
					cap->m_samples[n].m_data.push_back(s.m_data[0]);
					state = 11;
				}
				else
					state = 0;
				break;

			//Flags, frag offset
			case 11:
				if(s.m_type == EthernetFrameSegment::TYPE_PAYLOAD)
				{
					//Flags
					cap->m_offsets.push_back(din->m_offsets[i]);
					cap->m_durations.push_back(halfdur);
					cap->m_samples.push_back(IPv4Symbol(IPv4Symbol::TYPE_FLAGS, s.m_data[0] >> 5));

					//Frag offset, high 5 bits
					cap->m_offsets.push_back(din->m_offsets[i] + halfdur);
					cap->m_durations.push_back(halfdur);
					cap->m_samples.push_back(IPv4Symbol(IPv4Symbol::TYPE_FRAG_OFFSET, s.m_data[0] & 0x1f));
					state = 12;
				}
				else
					state = 0;
				break;
			case 12:
				if(s.m_type == EthernetFrameSegment::TYPE_PAYLOAD)
				{
					//Append to the previous sample
					size_t n = cap->m_offsets.size() - 1;
					cap->m_durations[n] = din->m_offsets[i] + din->m_durations[i] - cap->m_offsets[n];
					cap->m_samples[n].m_data.push_back(s.m_data[0]);
					state = 13;
				}
				else
					state = 0;
				break;

			//TTL
			case 13:
				if(s.m_type == EthernetFrameSegment::TYPE_PAYLOAD)
				{
					cap->m_offsets.push_back(din->m_offsets[i]);
					cap->m_durations.push_back(din->m_durations[i]);
					cap->m_samples.push_back(IPv4Symbol(IPv4Symbol::TYPE_TTL, s.m_data[0]));
					state = 14;
				}
				else
					state = 0;
				break;

			//Protocol
			case 14:
				if(s.m_type == EthernetFrameSegment::TYPE_PAYLOAD)
				{
					cap->m_offsets.push_back(din->m_offsets[i]);
					cap->m_durations.push_back(din->m_durations[i]);
					cap->m_samples.push_back(IPv4Symbol(IPv4Symbol::TYPE_PROTOCOL, s.m_data[0]));
					state = 15;
				}
				else
					state = 0;
				break;

			//Header checksum
			case 15:
				if(s.m_type == EthernetFrameSegment::TYPE_PAYLOAD)
				{
					cap->m_offsets.push_back(din->m_offsets[i]);
					cap->m_durations.push_back(din->m_durations[i]);
					cap->m_samples.push_back(IPv4Symbol(IPv4Symbol::TYPE_HEADER_CHECKSUM, s.m_data[0]));
					state = 16;
				}
				else
					state = 0;
				break;
			case 16:
				if(s.m_type == EthernetFrameSegment::TYPE_PAYLOAD)
				{
					//Append to the previous sample
					size_t n = cap->m_offsets.size() - 1;
					cap->m_durations[n] = din->m_offsets[i] + din->m_durations[i] - cap->m_offsets[n];
					cap->m_samples[n].m_data.push_back(s.m_data[0]);
					state = 17;
				}
				else
					state = 0;
				break;

			//Src IP
			case 17:
				if(s.m_type == EthernetFrameSegment::TYPE_PAYLOAD)
				{
					cap->m_offsets.push_back(din->m_offsets[i]);
					cap->m_durations.push_back(din->m_durations[i]);
					cap->m_samples.push_back(IPv4Symbol(IPv4Symbol::TYPE_SOURCE_IP, s.m_data[0]));
					state = 18;
				}
				else
					state = 0;
				break;
			case 18:
			case 19:
			case 20:
				if(s.m_type == EthernetFrameSegment::TYPE_PAYLOAD)
				{
					//Append to the previous sample
					size_t n = cap->m_offsets.size() - 1;
					cap->m_durations[n] = din->m_offsets[i] + din->m_durations[i] - cap->m_offsets[n];
					cap->m_samples[n].m_data.push_back(s.m_data[0]);
					state++;
				}
				else
					state = 0;
				break;

			//Dst IP
			case 21:
				if(s.m_type == EthernetFrameSegment::TYPE_PAYLOAD)
				{
					cap->m_offsets.push_back(din->m_offsets[i]);
					cap->m_durations.push_back(din->m_durations[i]);
					cap->m_samples.push_back(IPv4Symbol(IPv4Symbol::TYPE_DEST_IP, s.m_data[0]));
					state = 22;
				}
				else
					state = 0;
				break;
			case 22:
			case 23:
			case 24:
				if(s.m_type == EthernetFrameSegment::TYPE_PAYLOAD)
				{
					//Append to the previous sample
					size_t n = cap->m_offsets.size() - 1;
					cap->m_durations[n] = din->m_offsets[i] + din->m_durations[i] - cap->m_offsets[n];
					cap->m_samples[n].m_data.push_back(s.m_data[0]);
					state++;
				}
				else
					state = 0;
				break;

			//TODO: support header options
			case 25:
				if(s.m_type == EthernetFrameSegment::TYPE_PAYLOAD)
				{
					cap->m_offsets.push_back(din->m_offsets[i]);
					cap->m_durations.push_back(din->m_durations[i]);
					cap->m_samples.push_back(IPv4Symbol(IPv4Symbol::TYPE_DATA, s.m_data[0]));
				}

				//terminate the packet on FCS or error
				else
					state = 0;
				break;

		}
	}

	//TODO: packet decode too

	SetData(cap, 0);
	cap->MarkModifiedFromCpu();
}

std::string IPv4Waveform::GetColor(size_t i)
{
	switch(m_samples[i].m_type)
	{
		case IPv4Symbol::TYPE_VERSION:
		case IPv4Symbol::TYPE_HEADER_LEN:
			return StandardColors::colors[StandardColors::COLOR_PREAMBLE];

		case IPv4Symbol::TYPE_FLAGS:
		case IPv4Symbol::TYPE_DIFFSERV:
		case IPv4Symbol::TYPE_LENGTH:
		case IPv4Symbol::TYPE_ID:
		case IPv4Symbol::TYPE_FRAG_OFFSET:
		case IPv4Symbol::TYPE_TTL:
		case IPv4Symbol::TYPE_PROTOCOL:
		case IPv4Symbol::TYPE_OPTIONS:
			return StandardColors::colors[StandardColors::COLOR_CONTROL];

		//TODO: properly verify checksum
		case IPv4Symbol::TYPE_HEADER_CHECKSUM:
			return StandardColors::colors[StandardColors::COLOR_CHECKSUM_OK];

		case IPv4Symbol::TYPE_SOURCE_IP:
		case IPv4Symbol::TYPE_DEST_IP:
			return StandardColors::colors[StandardColors::COLOR_ADDRESS];

		case IPv4Symbol::TYPE_DATA:
			return StandardColors::colors[StandardColors::COLOR_DATA];

		case IPv4Symbol::TYPE_ERROR:
		default:
			return StandardColors::colors[StandardColors::COLOR_ERROR];
	}
}

string IPv4Waveform::GetText(size_t i)
{
	char tmp[128];

	auto sample = m_samples[i];
	switch(sample.m_type)
	{
		case IPv4Symbol::TYPE_VERSION:
			snprintf(tmp, sizeof(tmp), "V%d", sample.m_data[0]);
			return string(tmp);

		case IPv4Symbol::TYPE_HEADER_LEN:
			if(sample.m_data[0] == 5)
				return "No opts";
			else
			{
				snprintf(tmp, sizeof(tmp), "%d header words", sample.m_data[0]);
				return string(tmp);
			}

		case IPv4Symbol::TYPE_DIFFSERV:
			{
				snprintf(tmp, sizeof(tmp), "DSCP: %d", sample.m_data[0] >> 2);
				string ret = tmp;
				switch(sample.m_data[0] & 0x3)
				{
					case 0:
						ret += ", Non-ECT";
						break;

					case 1:
						ret += ", ECT(0)";
						break;

					case 2:
						ret += ", ECT(1)";
						break;

					case 3:
						ret += ", CE";
						break;
				}
				return ret;
			}

		case IPv4Symbol::TYPE_LENGTH:
			snprintf(tmp, sizeof(tmp), "Length: %d", (sample.m_data[0] << 8) | sample.m_data[1]);
			return string(tmp);

		case IPv4Symbol::TYPE_ID:
			snprintf(tmp, sizeof(tmp), "ID: 0x%04x", (sample.m_data[0] << 8) | sample.m_data[1]);
			return string(tmp);

		case IPv4Symbol::TYPE_FLAGS:
			{
				string ret;
				if(sample.m_data[0] & 4)
					ret = "Evil ";
				if(sample.m_data[0] & 2)
					ret += "DF ";
				if(sample.m_data[0] & 1)
					ret += "MF ";
				if(ret == "")
					ret = "No flag";
				return ret;
			}

		case IPv4Symbol::TYPE_FRAG_OFFSET:
			snprintf(tmp, sizeof(tmp), "Offset: 0x%04x", 8*( (sample.m_data[0] << 8) | sample.m_data[1]));
			return string(tmp);

		case IPv4Symbol::TYPE_TTL:
			snprintf(tmp, sizeof(tmp), "TTL: %d", sample.m_data[0]);
			return string(tmp);

		case IPv4Symbol::TYPE_PROTOCOL:
			switch(sample.m_data[0])
			{
				case 0x01:
					return "ICMP";
				case 0x02:
					return "IGMP";
				case 0x06:
					return "TCP";
				case 0x11:
					return "UDP";
				case 0x2f:
					return "GRE";
				case 0x58:
					return "EIGRP";
				case 0x59:
					return "OSPF";
				case 0x73:
					return "L2TP";
				case 0x85:
					return "FCoIP";

				default:
					snprintf(tmp, sizeof(tmp), "Protocol: 0x%02x", sample.m_data[0]);
					return string(tmp);
			}
			break;

		case IPv4Symbol::TYPE_HEADER_CHECKSUM:
			snprintf(tmp, sizeof(tmp), "Checksum: 0x%04x", (sample.m_data[0] << 8) | sample.m_data[1]);
			return string(tmp);

		case IPv4Symbol::TYPE_SOURCE_IP:
			snprintf(tmp, sizeof(tmp), "Source: %d.%d.%d.%d",
				sample.m_data[0], sample.m_data[1], sample.m_data[2], sample.m_data[3]);
			return string(tmp);

		case IPv4Symbol::TYPE_DEST_IP:
			snprintf(tmp, sizeof(tmp), "Dest: %d.%d.%d.%d",
				sample.m_data[0], sample.m_data[1], sample.m_data[2], sample.m_data[3]);
			return string(tmp);

		case IPv4Symbol::TYPE_DATA:
		case IPv4Symbol::TYPE_OPTIONS:
			snprintf(tmp, sizeof(tmp), "%02x", sample.m_data[0]);
			return string(tmp);

		case IPv4Symbol::TYPE_ERROR:
			return "ERROR";
	}

	return "";
}

