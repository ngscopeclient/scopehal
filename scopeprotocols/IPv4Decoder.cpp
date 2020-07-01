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
#include "IPv4Decoder.h"
#include "EthernetProtocolDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

IPv4Decoder::IPv4Decoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_MISC)
{
	//Set up channels
	m_signalNames.push_back("eth");
	m_channels.push_back(NULL);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool IPv4Decoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (dynamic_cast<EthernetProtocolDecoder*>(channel) != NULL) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

double IPv4Decoder::GetVoltageRange()
{
	return m_channels[0]->GetVoltageRange();
}

string IPv4Decoder::GetProtocolName()
{
	return "IPv4";
}

bool IPv4Decoder::IsOverlay()
{
	return true;
}

bool IPv4Decoder::NeedsConfig()
{
	//we just work on the provided ethernet link
	return false;
}

void IPv4Decoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "IPv4(%s)",	m_channels[0]->m_displayname.c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void IPv4Decoder::Refresh()
{
	//Get the input data
	if(m_channels[0] == NULL)
	{
		SetData(NULL);
		return;
	}
	auto din = dynamic_cast<EthernetWaveform*>(m_channels[0]->GetData());
	if(!din)
	{
		SetData(NULL);
		return;
	}

	//We need meaningful data
	size_t len = din->m_samples.size();
	if(len == 0)
	{
		SetData(NULL);
		return;
	}

	//Loop over the events and process stuff
	auto cap = new IPv4Waveform;
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startPicoseconds = din->m_startPicoseconds;

	int state = 0;
	int header_len = 0;
	for(size_t i=0; i<len; i++)
	{
		auto s = din->m_samples[i];

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
					int64_t halfdur = din->m_durations[i]/2;
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
		}
	}

	//TODO: packet decode too

	SetData(cap);
}

Gdk::Color IPv4Decoder::GetColor(int i)
{
	auto data = dynamic_cast<IPv4Waveform*>(GetData());
	if(data == NULL)
		return m_standardColors[COLOR_ERROR];
	if(i >= (int)data->m_samples.size())
		return m_standardColors[COLOR_ERROR];

	switch(data->m_samples[i].m_type)
	{
		case IPv4Symbol::TYPE_ERROR:
			return m_standardColors[COLOR_ERROR];

		case IPv4Symbol::TYPE_VERSION:
		case IPv4Symbol::TYPE_HEADER_LEN:
		case IPv4Symbol::TYPE_DIFFSERV:
		case IPv4Symbol::TYPE_LENGTH:
			return m_standardColors[COLOR_PREAMBLE];
	}
}

string IPv4Decoder::GetText(int i)
{
	auto data = dynamic_cast<IPv4Waveform*>(GetData());
	if(data == NULL)
		return "";
	if(i >= (int)data->m_samples.size())
		return "";

	char tmp[128];

	auto sample = data->m_samples[i];
	switch(sample.m_type)
	{
		case IPv4Symbol::TYPE_VERSION:
			snprintf(tmp, sizeof(tmp), "Version: %d", sample.m_data[0]);
			return string(tmp);

		case IPv4Symbol::TYPE_HEADER_LEN:
			snprintf(tmp, sizeof(tmp), "Header len: %d words", sample.m_data[0]);
			return string(tmp);

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
	}

	return "";
}

