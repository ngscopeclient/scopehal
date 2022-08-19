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
#include "TCPDecoder.h"
#include "EthernetProtocolDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

TCPDecoder::TCPDecoder(const string& color)
	: Filter(color, CAT_SERIAL)
{
	AddProtocolStream("data");
	CreateInput("ip");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool TCPDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (dynamic_cast<IPv4Waveform*>(stream.m_channel->GetData(0)) != NULL) )
		return true;

	//TODO: support IPv6

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string TCPDecoder::GetProtocolName()
{
	return "TCP";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void TCPDecoder::Refresh()
{
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data (TODO: support IPv6 too)
	auto din = dynamic_cast<IPv4Waveform*>(GetInputWaveform(0));
	size_t len = din->m_samples.size();

	//Loop over the events and process stuff
	auto cap = new TCPWaveform;
	cap->m_timescale = din->m_timescale;
	cap->m_triggerPhase = din->m_triggerPhase;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	SetData(cap, 0);

	int state = 0;
	int option_len = 0;
	for(size_t i=0; i<len; i++)
	{
		auto s = din->m_samples[i];
		size_t caplen = cap->m_samples.size();
		int64_t off = din->m_offsets[i];
		int64_t dur = din->m_durations[i];
		int64_t end = off + dur;
		int64_t halfdur = dur/2;

		uint8_t bin = 0;
		if(din->m_samples[i].m_data.size())
			bin = din->m_samples[i].m_data[0];

		switch(state)
		{
			//Wait for IP header version. Ignore any errors, preambles, etc before this
			case 0:
				break;

			//If we see a protocol other than TCP, discard and go back to beginning
			//TODO: add filtering to only show streams from/to specific hosts?
			case 1:
				if(s.m_type == IPv4Symbol::TYPE_PROTOCOL)
				{
					if(bin != 0x06)
						state = 0;
					else
						state = 2;
				}
				break;

			//Ignore all headers until we get to the start of the data field
			case 2:
				if(s.m_type == IPv4Symbol::TYPE_DATA)
				{
					state = 3;

					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(0);
					cap->m_samples.push_back(TCPSymbol(TCPSymbol::TYPE_SOURCE_PORT, bin));
				}
				break;

			//Read second half of source port
			case 3:
				if(s.m_type == IPv4Symbol::TYPE_DATA)
				{
					cap->m_samples[caplen-1].m_data.push_back(bin);
					cap->m_durations[caplen-1] = end - cap->m_offsets[caplen-1];
					state = 4;
				}
				else
					state = 0;
				break;

			//First half of dest port
			case 4:
				if(s.m_type == IPv4Symbol::TYPE_DATA)
				{
					state = 5;

					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(0);
					cap->m_samples.push_back(TCPSymbol(TCPSymbol::TYPE_DEST_PORT, bin));
				}
				else
					state = 0;
				break;

			//Second half of dest port
			case 5:
				if(s.m_type == IPv4Symbol::TYPE_DATA)
				{
					cap->m_samples[caplen-1].m_data.push_back(bin);
					cap->m_durations[caplen-1] = end - cap->m_offsets[caplen-1];
					state = 6;
				}
				else
					state = 0;
				break;

			//First byte of sequence number
			case 6:
				if(s.m_type == IPv4Symbol::TYPE_DATA)
				{
					state = 7;

					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(0);
					cap->m_samples.push_back(TCPSymbol(TCPSymbol::TYPE_SEQ, bin));
				}
				else
					state = 0;
				break;

			//Remainder of sequence number
			case 7:
				if(s.m_type == IPv4Symbol::TYPE_DATA)
				{
					cap->m_samples[caplen-1].m_data.push_back(bin);

					if(cap->m_samples[caplen-1].m_data.size() == 4)
					{
						cap->m_durations[caplen-1] = end - cap->m_offsets[caplen-1];
						state = 8;
					}
				}
				else
					state = 0;
				break;

			//First byte of ACK number
			case 8:
				if(s.m_type == IPv4Symbol::TYPE_DATA)
				{
					state = 9;

					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(0);
					cap->m_samples.push_back(TCPSymbol(TCPSymbol::TYPE_ACK, bin));
				}
				else
					state = 0;
				break;

			//Remainder of sequence number
			case 9:
				if(s.m_type == IPv4Symbol::TYPE_DATA)
				{
					cap->m_samples[caplen-1].m_data.push_back(bin);

					if(cap->m_samples[caplen-1].m_data.size() == 4)
					{
						cap->m_durations[caplen-1] = end - cap->m_offsets[caplen-1];
						state = 10;
					}
				}
				else
					state = 0;
				break;

			//Data offset
			case 10:
				if(s.m_type == IPv4Symbol::TYPE_DATA)
				{
					state = 11;

					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(halfdur);
					cap->m_samples.push_back(TCPSymbol(TCPSymbol::TYPE_DATA_OFFSET, bin >> 4));

					option_len = ( (bin >> 4) * 4) - 20;

					//Also push the NS bit of the flags
					cap->m_offsets.push_back(off + halfdur);
					cap->m_durations.push_back(0);
					cap->m_samples.push_back(TCPSymbol(TCPSymbol::TYPE_FLAGS, bin & 0xf));
				}
				else
					state = 0;
				break;

			//Rest of flags
			case 11:
				if(s.m_type == IPv4Symbol::TYPE_DATA)
				{
					cap->m_samples[caplen-1].m_data.push_back(bin);
					cap->m_durations[caplen-1] = end - cap->m_offsets[caplen-1];
					state = 12;
				}
				else
					state = 0;
				break;

			//First half of window size
			case 12:
				if(s.m_type == IPv4Symbol::TYPE_DATA)
				{
					state = 13;

					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(0);
					cap->m_samples.push_back(TCPSymbol(TCPSymbol::TYPE_WINDOW, bin));
				}
				else
					state = 0;
				break;

			//Second half of window size
			case 13:
				if(s.m_type == IPv4Symbol::TYPE_DATA)
				{
					cap->m_samples[caplen-1].m_data.push_back(bin);
					cap->m_durations[caplen-1] = end - cap->m_offsets[caplen-1];
					state = 14;
				}
				else
					state = 0;
				break;

			//First half of checksum
			case 14:
				if(s.m_type == IPv4Symbol::TYPE_DATA)
				{
					state = 15;

					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(0);
					cap->m_samples.push_back(TCPSymbol(TCPSymbol::TYPE_CHECKSUM, bin));
				}
				else
					state = 0;
				break;

			//Second half of checksum
			case 15:
				if(s.m_type == IPv4Symbol::TYPE_DATA)
				{
					cap->m_samples[caplen-1].m_data.push_back(bin);
					cap->m_durations[caplen-1] = end - cap->m_offsets[caplen-1];
					state = 16;
				}
				else
					state = 0;
				break;

			//First half of urgent pointer
			case 16:
				if(s.m_type == IPv4Symbol::TYPE_DATA)
				{
					state = 17;

					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(0);
					cap->m_samples.push_back(TCPSymbol(TCPSymbol::TYPE_URGENT, bin));
				}
				else
					state = 0;
				break;

			//Second half of urgent pointer
			case 17:
				if(s.m_type == IPv4Symbol::TYPE_DATA)
				{
					cap->m_samples[caplen-1].m_data.push_back(bin);
					cap->m_durations[caplen-1] = end - cap->m_offsets[caplen-1];
					state = 18;
				}
				else
					state = 0;
				break;

			//First byte of options or data
			case 18:
				if(s.m_type == IPv4Symbol::TYPE_DATA)
				{
					//No more options left? It's our first data byte
					if(option_len == 0)
					{
						cap->m_offsets.push_back(din->m_offsets[i]);
						cap->m_durations.push_back(din->m_durations[i]);
						cap->m_samples.push_back(TCPSymbol(TCPSymbol::TYPE_DATA, bin));

						state = 19;
					}

					//Nope, it's an option byte
					else
					{
						cap->m_offsets.push_back(din->m_offsets[i]);
						cap->m_durations.push_back(din->m_durations[i]);
						cap->m_samples.push_back(TCPSymbol(TCPSymbol::TYPE_OPTIONS, bin));

						option_len --;
					}
				}
				else
					state = 0;
				break;

			case 19:
				if(s.m_type == IPv4Symbol::TYPE_DATA)
				{
					cap->m_offsets.push_back(din->m_offsets[i]);
					cap->m_durations.push_back(din->m_durations[i]);
					cap->m_samples.push_back(TCPSymbol(TCPSymbol::TYPE_DATA, bin));
				}
				else
					state = 0;
				break;
		}

		//Reset when we see a new IP header starting
		if(s.m_type == IPv4Symbol::TYPE_VERSION)
			state = 1;
	}

	//TODO: packet decode too
}

Gdk::Color TCPWaveform::GetColor(size_t i)
{
	switch(m_samples[i].m_type)
	{
		case TCPSymbol::TYPE_SEQ:
		case TCPSymbol::TYPE_ACK:
		case TCPSymbol::TYPE_DATA_OFFSET:
		case TCPSymbol::TYPE_FLAGS:
		case TCPSymbol::TYPE_WINDOW:
		case TCPSymbol::TYPE_URGENT:
		case TCPSymbol::TYPE_OPTIONS:
			return StandardColors::colors[StandardColors::COLOR_CONTROL];

		//TODO: properly verify checksum
		case TCPSymbol::TYPE_CHECKSUM:
			return StandardColors::colors[StandardColors::COLOR_CHECKSUM_OK];

		case TCPSymbol::TYPE_SOURCE_PORT:
		case TCPSymbol::TYPE_DEST_PORT:
			return StandardColors::colors[StandardColors::COLOR_ADDRESS];

		case TCPSymbol::TYPE_DATA:
			return StandardColors::colors[StandardColors::COLOR_DATA];

		case TCPSymbol::TYPE_ERROR:
		default:
			return StandardColors::colors[StandardColors::COLOR_ERROR];
	}
}

string TCPWaveform::GetText(size_t i)
{
	char tmp[128];
	auto sample = m_samples[i];

	switch(sample.m_type)
	{
		case TCPSymbol::TYPE_SEQ:
			snprintf(tmp, sizeof(tmp), "Seq: %08x",
				(sample.m_data[0] << 24) | (sample.m_data[1] << 16) | (sample.m_data[2] << 8) | sample.m_data[3]);
			return string(tmp);

		case TCPSymbol::TYPE_ACK:
			snprintf(tmp, sizeof(tmp), "Ack: %08x",
				(sample.m_data[0] << 24) | (sample.m_data[1] << 16) | (sample.m_data[2] << 8) | sample.m_data[3]);
			return string(tmp);

		case TCPSymbol::TYPE_DATA_OFFSET:
			snprintf(tmp, sizeof(tmp), "Data off: %d", sample.m_data[0]);
			return string(tmp);

		case TCPSymbol::TYPE_FLAGS:
			{
				string s;
				if(sample.m_data[1] & 0x01)
					s += "FIN ";
				if(sample.m_data[1] & 0x02)
					s += "SYN ";
				if(sample.m_data[1] & 0x04)
					s += "RST ";
				if(sample.m_data[1] & 0x08)
					s += "PSH ";
				if(sample.m_data[1] & 0x10)
					s += "ACK ";
				if(sample.m_data[1] & 0x20)
					s += "URG ";
				if(sample.m_data[1] & 0x40)
					s += "ECE ";
				if(sample.m_data[1] & 0x80)
					s += "CWR ";
				if(sample.m_data[0] & 1)
					s += "NS ";
				return s;
			}

		case TCPSymbol::TYPE_WINDOW:
			snprintf(tmp, sizeof(tmp), "Window: %d", (sample.m_data[0] << 8) | sample.m_data[1]);
			return string(tmp);

		case TCPSymbol::TYPE_CHECKSUM:
			snprintf(tmp, sizeof(tmp), "Checksum: %x", (sample.m_data[0] << 8) | sample.m_data[1]);
			return string(tmp);

		case TCPSymbol::TYPE_URGENT:
			snprintf(tmp, sizeof(tmp), "Urgent: %x", (sample.m_data[0] << 8) | sample.m_data[1]);
			return string(tmp);

		case TCPSymbol::TYPE_SOURCE_PORT:
			snprintf(tmp, sizeof(tmp), "Source: %d", (sample.m_data[0] << 8) | sample.m_data[1]);
			return string(tmp);

		case TCPSymbol::TYPE_DEST_PORT:
			snprintf(tmp, sizeof(tmp), "Dest: %d",
				(sample.m_data[0] << 8) | sample.m_data[1]);
			return string(tmp);

		case TCPSymbol::TYPE_DATA:
		case TCPSymbol::TYPE_OPTIONS:
			snprintf(tmp, sizeof(tmp), "%02x", sample.m_data[0]);
			return string(tmp);

		case TCPSymbol::TYPE_ERROR:
		default:
			return "ERROR";
	}

	return "";
}
