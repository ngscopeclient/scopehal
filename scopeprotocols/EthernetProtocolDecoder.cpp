/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2020 Andrew D. Zonenberg                                                                          *
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
#include "EthernetProtocolDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

EthernetProtocolDecoder::EthernetProtocolDecoder(string color)
	: PacketDecoder(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_SERIAL)
{
	//Set up channels
	m_signalNames.push_back("din");
	m_channels.push_back(NULL);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool EthernetProtocolDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

bool EthernetProtocolDecoder::NeedsConfig()
{
	//No config needed
	return false;
}

vector<string> EthernetProtocolDecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("Dest MAC");
	ret.push_back("Src MAC");
	ret.push_back("Ethertype");
	return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual protocol decoding

void EthernetProtocolDecoder::BytesToFrames(
		vector<uint8_t>& bytes,
		vector<uint64_t>& starts,
		vector<uint64_t>& ends,
		EthernetWaveform* cap)
{
	Packet* pack = NULL;

	EthernetFrameSegment segment;
	size_t start = 0;
	for(size_t i=0; i<bytes.size(); i++)
	{
		switch(segment.m_type)
		{
			case EthernetFrameSegment::TYPE_INVALID:

				//In between frames. Look for a preamble
				if(bytes[i] != 0x55)
				{
					//LogDebug("EthernetProtocolDecoder: Skipping unknown byte %02x\n", bytes[i]);
				}

				//Got a valid 55. We're now in the preamble
				else
				{
					start = starts[i] / cap->m_timescale;
					segment.m_type = EthernetFrameSegment::TYPE_PREAMBLE;
					segment.m_data.clear();
					segment.m_data.push_back(0x55);

					//Start a new packet
					pack = new Packet;
					pack->m_offset = starts[i];
				}
				break;

			case EthernetFrameSegment::TYPE_PREAMBLE:

				//TODO: Verify that this byte is immediately after the previous one

				//Look for the SFD
				if(bytes[i] == 0xd5)
				{
					//Save the preamble
					cap->m_offsets.push_back(start);
					cap->m_durations.push_back( (starts[i] / cap->m_timescale) - start);
					cap->m_samples.push_back(segment);

					//Save the SFD
					start = starts[i] / cap->m_timescale;
					cap->m_offsets.push_back(start);
					cap->m_durations.push_back( (ends[i] / cap->m_timescale) - start);
					segment.m_type = EthernetFrameSegment::TYPE_SFD;
					segment.m_data.clear();
					segment.m_data.push_back(0xd5);
					cap->m_samples.push_back(segment);

					//Set up for data
					segment.m_type = EthernetFrameSegment::TYPE_DST_MAC;
					segment.m_data.clear();
				}

				//No SFD, just add the preamble byte
				else if(bytes[i] == 0x55)
					segment.m_data.push_back(0x55);

				//Garbage (TODO: handle this better)
				else
				{
					//LogDebug("EthernetProtocolDecoder: Skipping unknown byte %02x\n", bytes[i]);
				}

				break;

			case EthernetFrameSegment::TYPE_DST_MAC:

				//Start of MAC? Record start time
				if(segment.m_data.empty())
				{
					start = starts[i] / cap->m_timescale;
					cap->m_offsets.push_back(start);
				}

				//Add the data
				segment.m_data.push_back(bytes[i]);

				//Are we done? Add it
				if(segment.m_data.size() == 6)
				{
					cap->m_durations.push_back( (ends[i] / cap->m_timescale) - start);
					cap->m_samples.push_back(segment);

					//Reset for next block of the frame
					segment.m_type = EthernetFrameSegment::TYPE_SRC_MAC;
					segment.m_data.clear();

					//Format the content for display
					char tmp[64];
					snprintf(tmp, sizeof(tmp), "%02x:%02x:%02x:%02x:%02x:%02x",
						segment.m_data[0],
						segment.m_data[1],
						segment.m_data[2],
						segment.m_data[3],
						segment.m_data[4],
						segment.m_data[5]);
					pack->m_headers["Dest MAC"] = tmp;
				}

				break;

			case EthernetFrameSegment::TYPE_SRC_MAC:

				//Start of MAC? Record start time
				if(segment.m_data.empty())
				{
					start = starts[i] / cap->m_timescale;
					cap->m_offsets.push_back(start);
				}

				//Add the data
				segment.m_data.push_back(bytes[i]);

				//Are we done? Add it
				if(segment.m_data.size() == 6)
				{
					cap->m_durations.push_back( (ends[i] / cap->m_timescale) - start);
					cap->m_samples.push_back(segment);

					//Reset for next block of the frame
					segment.m_type = EthernetFrameSegment::TYPE_ETHERTYPE;
					segment.m_data.clear();

					//Format the content for display
					char tmp[64];
					snprintf(tmp, sizeof(tmp),"%02x:%02x:%02x:%02x:%02x:%02x",
						segment.m_data[0],
						segment.m_data[1],
						segment.m_data[2],
						segment.m_data[3],
						segment.m_data[4],
						segment.m_data[5]);
					pack->m_headers["Src MAC"] = tmp;
				}

				break;

			case EthernetFrameSegment::TYPE_ETHERTYPE:

				//Start of Ethertype? Record start time
				if(segment.m_data.empty())
				{
					start = starts[i] / cap->m_timescale;
					cap->m_offsets.push_back(start);
				}

				//Add the data
				segment.m_data.push_back(bytes[i]);

				//Are we done? Add it
				if(segment.m_data.size() == 2)
				{
					cap->m_durations.push_back( (ends[i] / cap->m_timescale) - start);
					cap->m_samples.push_back(segment);

					//Reset for next block of the frame
					segment.m_type = EthernetFrameSegment::TYPE_PAYLOAD;
					segment.m_data.clear();

					//Format the content for display
					uint16_t ethertype = (segment.m_data[0] << 8) | segment.m_data[1];
					char tmp[64];
					switch(ethertype)
					{
						case 0x0800:
							pack->m_headers["Ethertype"] = "IPv4";
							break;

						case 0x0806:
							pack->m_headers["Ethertype"] = "ARP";
							break;

						case 0x8100:
							pack->m_headers["Ethertype"] = "802.1q";
							break;

						case 0x86DD:
							pack->m_headers["Ethertype"] = "IPv6";
							break;

						default:
							snprintf(tmp, sizeof(tmp), "%02x%02x",
							segment.m_data[0],
							segment.m_data[1]);
							pack->m_headers["Ethertype"] = tmp;
							break;
					}
				}

				break;

			case EthernetFrameSegment::TYPE_PAYLOAD:

				//Add a data element
				//For now, each byte is its own payload blob
				start = starts[i] / cap->m_timescale;
				cap->m_offsets.push_back(start);
				cap->m_durations.push_back( (ends[i] / cap->m_timescale) - start);
				segment.m_type = EthernetFrameSegment::TYPE_PAYLOAD;
				segment.m_data.clear();
				segment.m_data.push_back(bytes[i]);
				cap->m_samples.push_back(segment);

				//If almost at end of packet, next 4 bytes are FCS
				if(i == bytes.size() - 5)
				{
					segment.m_data.clear();
					segment.m_type = EthernetFrameSegment::TYPE_FCS;
				}
				else
					pack->m_data.push_back(bytes[i]);
				break;

			case EthernetFrameSegment::TYPE_FCS:

				//Start of FCS? Record start time
				if(segment.m_data.empty())
				{
					start = starts[i] / cap->m_timescale;
					cap->m_offsets.push_back(start);
				}

				//Add the data
				segment.m_data.push_back(bytes[i]);

				//Are we done? Add it
				if(segment.m_data.size() == 4)
				{
					cap->m_durations.push_back( (ends[i] / cap->m_timescale) - start);
					cap->m_samples.push_back(segment);

					pack->m_len = ends[i] - pack->m_offset;
					m_packets.push_back(pack);
				}

				break;

			default:
				break;
		}
	}
}

Gdk::Color EthernetProtocolDecoder::GetColor(int i)
{
	auto data = dynamic_cast<EthernetWaveform*>(GetData());
	if(data == NULL)
		return m_standardColors[COLOR_ERROR];
	if(i >= (int)data->m_samples.size())
		return m_standardColors[COLOR_ERROR];

	switch(data->m_samples[i].m_type)
	{
		//Preamble/SFD: gray (not interesting)
		case EthernetFrameSegment::TYPE_PREAMBLE:
			return m_standardColors[COLOR_PREAMBLE];
		case EthernetFrameSegment::TYPE_SFD:
			return m_standardColors[COLOR_PREAMBLE];

		//MAC addresses (src or dest)
		case EthernetFrameSegment::TYPE_DST_MAC:
		case EthernetFrameSegment::TYPE_SRC_MAC:
			return m_standardColors[COLOR_ADDRESS];

		//Control codes
		case EthernetFrameSegment::TYPE_ETHERTYPE:
		case EthernetFrameSegment::TYPE_VLAN_TAG:
			return m_standardColors[COLOR_CONTROL];

		//TODO: verify checksum
		case EthernetFrameSegment::TYPE_FCS:
			return m_standardColors[COLOR_CHECKSUM_OK];

		//Signal has entirely disappeared
		case EthernetFrameSegment::TYPE_NO_CARRIER:
			return m_standardColors[COLOR_ERROR];

		//Payload
		default:
			return m_standardColors[COLOR_DATA];
	}
}

string EthernetProtocolDecoder::GetText(int i)
{
	auto data = dynamic_cast<EthernetWaveform*>(GetData());
	if(data == NULL)
		return "";
	if(i >= (int)data->m_samples.size())
		return "";

	auto sample = data->m_samples[i];
	switch(sample.m_type)
	{
		case EthernetFrameSegment::TYPE_PREAMBLE:
			return "PREAMBLE";

		case EthernetFrameSegment::TYPE_SFD:
			return "SFD";

		case EthernetFrameSegment::TYPE_NO_CARRIER:
			return "NO CARRIER";

		case EthernetFrameSegment::TYPE_DST_MAC:
			{
				if(sample.m_data.size() != 6)
					return "[invalid dest MAC length]";

				char tmp[32];
				snprintf(tmp, sizeof(tmp), "Dest MAC: %02x:%02x:%02x:%02x:%02x:%02x",
					sample.m_data[0],
					sample.m_data[1],
					sample.m_data[2],
					sample.m_data[3],
					sample.m_data[4],
					sample.m_data[5]);
				return tmp;
			}

		case EthernetFrameSegment::TYPE_SRC_MAC:
			{
				if(sample.m_data.size() != 6)
					return "[invalid src MAC length]";

				char tmp[32];
				snprintf(tmp, sizeof(tmp), "Src MAC: %02x:%02x:%02x:%02x:%02x:%02x",
					sample.m_data[0],
					sample.m_data[1],
					sample.m_data[2],
					sample.m_data[3],
					sample.m_data[4],
					sample.m_data[5]);
				return tmp;
			}

		case EthernetFrameSegment::TYPE_ETHERTYPE:
			{
				if(sample.m_data.size() != 2)
					return "[invalid Ethertype length]";

				string type = "Type: ";

				char tmp[32];
				uint16_t ethertype = (sample.m_data[0] << 8) | sample.m_data[1];
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
				for(auto b : sample.m_data)
				{
					char tmp[32];
					snprintf(tmp, sizeof(tmp), "%02x ", b);
					ret += tmp;
				}
				return ret;
			}

		case EthernetFrameSegment::TYPE_FCS:
			{
				if(sample.m_data.size() != 4)
					return "[invalid FCS length]";

				char tmp[32];
				snprintf(tmp, sizeof(tmp), "CRC: %02x%02x%02x%02x",
					sample.m_data[0],
					sample.m_data[1],
					sample.m_data[2],
					sample.m_data[3]);
				return tmp;
			}

		default:
			break;
	}

	return "";
}
