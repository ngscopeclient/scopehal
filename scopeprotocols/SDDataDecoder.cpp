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
	@brief Implementation of SDDataDecoder
 */

#include "../scopehal/scopehal.h"
#include "SDDataDecoder.h"
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SDDataDecoder::SDDataDecoder(const string& color)
	: PacketDecoder(color, CAT_MEMORY)
{
	CreateInput("clk");
	CreateInput("dat3");
	CreateInput("dat2");
	CreateInput("dat1");
	CreateInput("dat0");
	CreateInput("cmdbus");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

string SDDataDecoder::GetProtocolName()
{
	return "SD Card Data Bus";
}

bool SDDataDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 5) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	if( (i == 5) && (dynamic_cast<SDCmdDecoder*>(stream.m_channel) != NULL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void SDDataDecoder::Refresh()
{
	ClearPackets();

	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto clk = GetDigitalInputWaveform(0);
	auto data3 = GetDigitalInputWaveform(1);
	auto data2 = GetDigitalInputWaveform(2);
	auto data1 = GetDigitalInputWaveform(3);
	auto data0 = GetDigitalInputWaveform(4);
	auto cmdbus = dynamic_cast<SDCmdDecoder*>(GetInput(5).m_channel);

	//Sample the data
	DigitalWaveform d0;
	DigitalWaveform d1;
	DigitalWaveform d2;
	DigitalWaveform d3;
	SampleOnRisingEdges(data0, clk, d0);
	SampleOnRisingEdges(data1, clk, d1);
	SampleOnRisingEdges(data2, clk, d2);
	SampleOnRisingEdges(data3, clk, d3);
	size_t len = min(d0.m_samples.size(), d1.m_samples.size());
	len = min(len, d2.m_samples.size());
	len = min(len, d3.m_samples.size());

	//Create the capture
	auto cap = new SDDataWaveform;
	cap->m_timescale = 1;
	cap->m_startTimestamp = clk->m_startTimestamp;
	cap->m_startFemtoseconds = clk->m_startFemtoseconds;

	Packet* pack = NULL;
	Packet* last_cmdbus_packet = NULL;

	//Loop over the data and look for transactions
	enum
	{
		STATE_IDLE,
		STATE_DATA_HIGH,
		STATE_DATA_LOW,
		STATE_CRC,
		STATE_END
	} state = STATE_IDLE;

	int bytes_left = 0;

	int64_t data_start = 0;
	uint8_t high_val = 0;
	for(size_t i=0; i<len; i++)
	{
		uint8_t cur_data =
			(d3.m_samples[i] ? 0x8 : 0) |
			(d2.m_samples[i] ? 0x4 : 0) |
			(d1.m_samples[i] ? 0x2 : 0) |
			(d0.m_samples[i] ? 0x1 : 0);

		switch(state)
		{
			case STATE_IDLE:

				//Start of frame
				if(cur_data == 0x0)
				{
					cap->m_offsets.push_back(d0.m_offsets[i]);
					cap->m_durations.push_back(d0.m_durations[i]);
					cap->m_samples.push_back(SDDataSymbol(SDDataSymbol::TYPE_START, 0));
					bytes_left = 512;
					state = STATE_DATA_HIGH;

					//Find the command bus packet that triggered this data bus transaction
					auto cmd_packet = FindCommandBusPacket(cmdbus, d0.m_offsets[i]);

					//If it's the same as our last packet, or doesn't exist, don't make a new packet
					if(cmd_packet == NULL)
						pack = NULL;

					else if(cmd_packet == last_cmdbus_packet)
					{}

					//New packet, process stuff
					else
					{
						pack = new Packet;
						pack->m_offset = d0.m_offsets[i];
						pack->m_len = 0;
						pack->m_headers = cmd_packet->m_headers;
						pack->m_displayForegroundColor = cmd_packet->m_displayForegroundColor;
						pack->m_displayBackgroundColor = cmd_packet->m_displayBackgroundColor;
						m_packets.push_back(pack);

						last_cmdbus_packet = cmd_packet;
					}
				}

				//Idle
				else if(cur_data == 0xf)
				{}

				//Garbage, ignore

				break;

			case STATE_DATA_HIGH:
				data_start = d0.m_offsets[i];
				high_val = cur_data << 4;
				state = STATE_DATA_LOW;
				break;

			case STATE_DATA_LOW:
				{
					uint8_t data = high_val | cur_data;

					cap->m_offsets.push_back(data_start);
					cap->m_durations.push_back(d0.m_offsets[i] + d0.m_durations[i] - data_start);
					cap->m_samples.push_back(SDDataSymbol(SDDataSymbol::TYPE_DATA, data));

					if(pack)
						pack->m_data.push_back(data);

					bytes_left --;

					if(bytes_left > 0)
						state = STATE_DATA_HIGH;
					else
					{
						data_start = d0.m_offsets[i] + d0.m_durations[i];
						state = STATE_CRC;
						bytes_left = 16;
					}
				}
				break;

			case STATE_CRC:

				bytes_left --;

				if(bytes_left == 0)
				{
					//TODO: actually check CRCs
					cap->m_offsets.push_back(data_start);
					cap->m_durations.push_back(d0.m_offsets[i] + d0.m_durations[i] - data_start);

					cap->m_samples.push_back(SDDataSymbol(SDDataSymbol::TYPE_CRC_OK, 0));
					state = STATE_END;
				}

				break;

			case STATE_END:
				cap->m_offsets.push_back(d0.m_offsets[i]);
				cap->m_durations.push_back(d0.m_durations[i]);
				cap->m_samples.push_back(SDDataSymbol(SDDataSymbol::TYPE_END, 0));
				state = STATE_IDLE;

				if(pack)
					pack->m_len = d0.m_offsets[i] + d0.m_durations[i] - pack->m_offset;

				break;

			default:
				break;
		}
	}

	SetData(cap, 0);
}

Gdk::Color SDDataDecoder::GetColor(size_t i, size_t /*stream*/)
{
	auto capture = dynamic_cast<SDDataWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const SDDataSymbol& s = capture->m_samples[i];
		switch(s.m_stype)
		{
			case SDDataSymbol::TYPE_START:
			case SDDataSymbol::TYPE_END:
				return m_standardColors[COLOR_PREAMBLE];

			case SDDataSymbol::TYPE_CRC_OK:
				return m_standardColors[COLOR_CHECKSUM_OK];

			case SDDataSymbol::TYPE_CRC_BAD:
				return m_standardColors[COLOR_CHECKSUM_BAD];

			case SDDataSymbol::TYPE_DATA:
				return m_standardColors[COLOR_DATA];

			case SDDataSymbol::TYPE_ERROR:
			default:
				return m_standardColors[COLOR_ERROR];
		}
	}
	return m_standardColors[COLOR_ERROR];
}

string SDDataDecoder::GetText(size_t i, size_t /*stream*/)
{
	auto capture = dynamic_cast<SDDataWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const SDDataSymbol& s = capture->m_samples[i];
		char tmp[32];
		switch(s.m_stype)
		{
			case SDDataSymbol::TYPE_START:
				return "START";

			case SDDataSymbol::TYPE_END:
				return "END";

			case SDDataSymbol::TYPE_CRC_OK:
			case SDDataSymbol::TYPE_CRC_BAD:
				return "CRC FIXME";

			case SDDataSymbol::TYPE_DATA:
				snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
				return string(tmp);

			case SDDataSymbol::TYPE_ERROR:
			default:
				return "ERROR";
		}
	}
	return "";
}

vector<string> SDDataDecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("Type");
	ret.push_back("Code");
	ret.push_back("Command");
	ret.push_back("Info");
	return ret;
}

Packet* SDDataDecoder::FindCommandBusPacket(SDCmdDecoder* decode, int64_t timestamp)
{
	//TODO: more efficient than linear search
	Packet* ret = NULL;
	auto packets = decode->GetPackets();
	for(auto p : packets)
	{
		//If it's not a command, ignore it
		if(p->m_headers["Type"] != "Command")
			continue;

		//If it's after the timestamp, we're done
		if(p->m_offset > timestamp)
			break;

		ret = p;

	}
	return ret;
}
