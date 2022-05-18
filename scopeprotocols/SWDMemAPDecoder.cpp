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
#include "SWDMemAPDecoder.h"
#include "SWDDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SWDMemAPDecoder::SWDMemAPDecoder(const string& color)
	: PacketDecoder(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_MEMORY)
{
	CreateInput("swd");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool SWDMemAPDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (dynamic_cast<SWDDecoder*>(stream.m_channel) != NULL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

vector<string> SWDMemAPDecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("Op");
	ret.push_back("Address");
	ret.push_back("Data");
	return ret;
}

string SWDMemAPDecoder::GetProtocolName()
{
	return "SWD MEM-AP";
}

bool SWDMemAPDecoder::NeedsConfig()
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void SWDMemAPDecoder::Refresh()
{
	ClearPackets();

	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	auto din = dynamic_cast<SWDWaveform*>(GetInputWaveform(0));
	if(!din)
	{
		SetData(NULL, 0);
		return;
	}

	//Set up output
	auto cap = new SWDMemAPWaveform;
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;

	//Main decode loop
	SWDMemAPSymbol samp;
	size_t len = din->m_samples.size();
	enum
	{
		STATE_IDLE,
		STATE_TYPE,
		STATE_RW,
		STATE_ADDRESS,
		STATE_HEADER_PARITY,
		STATE_STOP,
		STATE_PARK,
		STATE_HEADER_TURNAROUND,
		STATE_ACK,
		STATE_DATA_TURNAROUND,
		STATE_DATA,
		STATE_DATA_PARITY
	} state = STATE_IDLE;
	int64_t packstart = 0;
	int64_t tstart = 0;
	uint8_t reg_addr = 0;
	uint32_t reg_data = 0;
	uint32_t tar = 0;
	bool reading = true;
	bool access_is_ap = false;
	bool first_read = false;
	char tmp[128];
	Packet* pack = NULL;
	for(size_t i=0; i<len; i++)
	{
		auto s = din->m_samples[i];
		int64_t end = din->m_offsets[i] + din->m_durations[i];

		switch(state)
		{
			//Expect a start bit, ignore anything before that
			case STATE_IDLE:
				if(s.m_stype == SWDSymbol::TYPE_START)
				{
					packstart = din->m_offsets[i];
					state = STATE_TYPE;

					//Create a new packet. If we already have an incomplete one that got aborted, reset it
					if(pack)
					{
						pack->m_data.clear();
						pack->m_headers.clear();
					}
					else
						pack = new Packet;

					pack->m_offset = din->m_offsets[i] * din->m_timescale;
					pack->m_len = 0;
				}
				break;

			//Next should be AP/DP type selection
			case STATE_TYPE:
				if(s.m_stype == SWDSymbol::TYPE_AP_NDP)
				{
					//For now, assume we're talking to a MEM-AP if it's an AP
					//(no JTAG-AP support)
					if(s.m_data == 1)
						access_is_ap = true;
					else
						access_is_ap = false;

					state = STATE_RW;
				}
				else
					state = STATE_IDLE;
				break;

			//Next should be read/write bit
			case STATE_RW:
				if(s.m_stype == SWDSymbol::TYPE_R_NW)
				{
					reading = s.m_data;
					state = STATE_ADDRESS;
				}
				else
					state = STATE_IDLE;
				break;

			//AP register
			case STATE_ADDRESS:
				if(s.m_stype == SWDSymbol::TYPE_ADDRESS)
				{
					reg_addr = s.m_data;
					state = STATE_HEADER_PARITY;
				}
				else
					state = STATE_IDLE;
				break;

			//Header parity bit
			case STATE_HEADER_PARITY:
				if(s.m_stype == SWDSymbol::TYPE_PARITY_OK)
					state = STATE_STOP;
				else
					state = STATE_IDLE;
				break;

			case STATE_STOP:
				if(s.m_stype == SWDSymbol::TYPE_STOP)
					state = STATE_PARK;
				else
					state = STATE_IDLE;
				break;
			case STATE_PARK:
				if(s.m_stype == SWDSymbol::TYPE_PARK)
					state = STATE_HEADER_TURNAROUND;
				else
					state = STATE_IDLE;
				break;

			case STATE_HEADER_TURNAROUND:
				if(s.m_stype == SWDSymbol::TYPE_TURNAROUND)
					state = STATE_ACK;
				else
					state = STATE_IDLE;
				break;

			case STATE_ACK:
				if(s.m_stype == SWDSymbol::TYPE_ACK)
				{
					//Anything but an ACK means the transaction didn't go through
					if(s.m_data != 1)
						state = STATE_IDLE;

					else if(reading)
						state = STATE_DATA;
					else
						state = STATE_DATA_TURNAROUND;
				}
				else
					state = STATE_IDLE;
				break;

			case STATE_DATA_TURNAROUND:
				if(s.m_stype == SWDSymbol::TYPE_TURNAROUND)
					state = STATE_DATA;
				else
					state = STATE_IDLE;
				break;

			case STATE_DATA:
				if(s.m_stype == SWDSymbol::TYPE_DATA)
				{
					reg_data = s.m_data;
					state = STATE_DATA_PARITY;
				}
				else
					state = STATE_IDLE;
				break;

			case STATE_DATA_PARITY:
				if(s.m_stype == SWDSymbol::TYPE_PARITY_OK)
				{
					bool mem_access = false;

					if(access_is_ap)
					{
						//MEM-AP DRW
						if(reg_addr == 0xc)
						{
							//This is the first time we've read DRW.
							//This initiates the read, but doesn't actually give us the data.
							if(first_read && reading)
								first_read = false;

							else
								mem_access = true;
						}

						//MEM-AP TAR
						//Save transfer address, not actually doing a memory access
						if(!reading && (reg_addr == 0x4) )
						{
							tar = reg_data;
							first_read = true;
							tstart = packstart;
						}
					}

					else
					{
						//DP register 4 is CTRL/STAT, ignore

						//TODO: DP register 8 read is READ RESEND
						//DP register 8 write is AP SELECT, ignore

						//SW-DP RDBUFF
						if(reg_addr == 0xc)
							mem_access = true;
					}

					//This transaction completes a memory access
					if(mem_access)
					{
						cap->m_offsets.push_back(tstart);
						cap->m_durations.push_back(end - tstart);
						cap->m_samples.push_back(SWDMemAPSymbol(!reading, tar, reg_data));

						if(reading)
						{
							pack->m_headers["Op"] = "Read";
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
						}
						else
						{
							pack->m_headers["Op"] = "Write";
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
						}
						snprintf(tmp, sizeof(tmp), "%08x", tar);
						pack->m_headers["Address"] = tmp;
						snprintf(tmp, sizeof(tmp), "%08x", reg_data);
						pack->m_headers["Data"] = tmp;
						m_packets.push_back(pack);
						pack = NULL;

						tstart = end;
					}

					//Done (ignore turnaround after data)
					state = STATE_IDLE;
				}
				else
					state = STATE_IDLE;
				break;
		}
	}

	if(pack)
		delete pack;

	SetData(cap, 0);
}

Gdk::Color SWDMemAPDecoder::GetColor(int /*i*/)
{
	auto capture = dynamic_cast<SWDMemAPWaveform*>(GetData(0));
	if(capture != NULL)
	{
		//const SWDMemAPSymbol& s = capture->m_samples[i];
		return m_standardColors[COLOR_DATA];
	}

	return m_standardColors[COLOR_ERROR];
}

string SWDMemAPDecoder::GetText(int i)
{
	auto capture = dynamic_cast<SWDMemAPWaveform*>(GetData(0));
	char tmp[128] = "";
	if(capture != NULL)
	{
		const SWDMemAPSymbol& s = capture->m_samples[i];

		if(s.m_write)
			snprintf(tmp, sizeof(tmp), "Write %08x: %08x", s.m_addr, s.m_data);
		else
			snprintf(tmp, sizeof(tmp), "Read %08x: %08x", s.m_addr, s.m_data);
		return string(tmp);
	}

	return string(tmp);
}
