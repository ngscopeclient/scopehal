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
#include "DPhyDataDecoder.h"
#include "DPhySymbolDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DPhyDataDecoder::DPhyDataDecoder(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_SERIAL)
{
	CreateInput("Clock");
	CreateInput("Data");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DPhyDataDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 2) && (dynamic_cast<DPhySymbolDecoder*>(stream.m_channel) != NULL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

double DPhyDataDecoder::GetVoltageRange()
{
	return m_inputs[0].m_channel->GetVoltageRange();
}

string DPhyDataDecoder::GetProtocolName()
{
	return "MIPI D-PHY Data";
}

bool DPhyDataDecoder::IsOverlay()
{
	return true;
}

bool DPhyDataDecoder::NeedsConfig()
{
	return true;
}

void DPhyDataDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "DPhyData(%s, %s)",
		GetInputDisplayName(0).c_str(), GetInputDisplayName(1).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DPhyDataDecoder::Refresh()
{
	//Sanity check
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}
	auto clk = dynamic_cast<DPhySymbolWaveform*>(GetInputWaveform(0));
	auto data = dynamic_cast<DPhySymbolWaveform*>(GetInputWaveform(1));

	//Create output waveform
	auto cap = new DPhyDataWaveform;
	cap->m_timescale = data->m_timescale;
	cap->m_startTimestamp = data->m_startTimestamp;
	cap->m_startPicoseconds = data->m_startPicoseconds;

	enum
	{
		STATE_UNKNOWN,
		STATE_IDLE,
		STATE_HS_REQUEST,
		STATE_HS_SYNC_0,
		STATE_HS_SYNC_1,
		STATE_HS_SYNC_2,
		STATE_HS_SYNC_3,
		STATE_HS_SYNC_4,
		STATE_HS_DATA
	} state = STATE_UNKNOWN;

	//Process the data
	DPhyDataSymbol samp;
	size_t clklen = clk->m_samples.size();
	size_t datalen = data->m_samples.size();
	size_t iclk = 0;
	size_t idata = 0;
	int64_t timestamp	= 0;
	bool last_clk = 0;
	int count = 0;
	uint8_t cur_byte = 0;
	int64_t tstart = 0;
	while(true)
	{
		//Get the current samples
		auto cur_clk = clk->m_samples[iclk];
		auto cur_data = data->m_samples[idata];

		//Get timestamps of next event on each channel
		int64_t next_data = GetNextEventTimestamp(data, idata, datalen, timestamp);
		int64_t next_clk = GetNextEventTimestamp(clk, iclk, clklen, timestamp);
		int64_t next_timestamp = min(next_clk, next_data);
		if(next_timestamp == timestamp)
			break;

		size_t nlast = cap->m_samples.size()-1;
		int64_t tend = data->m_offsets[idata] + data->m_durations[idata];
		int64_t tclkstart = clk->m_offsets[iclk];
		int64_t tclkend = tclkstart + clk->m_durations[iclk];

		//Look for clock edges
		bool clock_rising = false;
		bool clock_falling = false;
		if(cur_clk.m_type == DPhySymbol::STATE_HS1)
		{
			if(!last_clk)
				clock_rising = true;
			last_clk = true;
		}
		else if(cur_clk.m_type == DPhySymbol::STATE_HS0)
		{
			if(last_clk)
				clock_falling = true;
			last_clk = false;
		}
		bool clock_toggling = clock_rising || clock_falling;

		switch(state)
		{
			//Just started decoding. We don't know what's going on.
			//Wait for the link to go idle.
			case STATE_UNKNOWN:

				//LP-11 is a STOP sequence. The partial packet before this point can be safely discarded.
				//Emit an "IDLE" state for the duration of the LP-11.
				if(cur_data.m_type == DPhySymbol::STATE_LP11)
				{
					state = STATE_IDLE;
					timestamp = tend;
				}
				break;	//end STATE_UNKNOWN

			//Link is idle, wait for a start-of-transmission or escape sequence
			case STATE_IDLE:

				//LP-01 is a HS-REQUEST
				if(cur_data.m_type == DPhySymbol::STATE_LP01)
				{
					state = STATE_HS_REQUEST;

					cap->m_offsets.push_back(data->m_offsets[idata]);
					cap->m_durations.push_back(data->m_durations[idata]);
					cap->m_samples.push_back(DPhyDataSymbol(DPhyDataSymbol::TYPE_SOT));

					timestamp = tend;
				}

				break;	//end STATE_IDLE

			//Starting a start-of-transmission sequence
			case STATE_HS_REQUEST:

				switch(cur_data.m_type)
				{
					//Ignore any LP states other than LP-11 which resets us
					case DPhySymbol::STATE_LP11:
						state = STATE_IDLE;
						timestamp = tend;
						break;

					//If we see HS-0, we're in the sync stage
					case DPhySymbol::STATE_HS0:
						state = STATE_HS_SYNC_0;
						break;

					default:
						break;
				}

				break;	//end STATE_HS_REQUEST

			//Wait for a HS-1 state on a rising clock edge to continue the sync
			case STATE_HS_SYNC_0:

				//Reset on LP-11
				if(cur_data.m_type == DPhySymbol::STATE_LP11)
				{
					state = STATE_IDLE;
					timestamp = tend;
					break;
				}

				//We got the HS-1. Extend the sample.
				if(clock_falling && cur_data.m_type == DPhySymbol::STATE_HS1)
				{
					state = STATE_HS_SYNC_1;
					count = 1;

					cap->m_durations[nlast] = tclkstart - cap->m_offsets[nlast];
				}

				break;	//end STATE_HS_SYNC_0

			//Expect three HS-1's in a row.
			case STATE_HS_SYNC_1:
				if(clock_toggling)
				{
					if(cur_data.m_type == DPhySymbol::STATE_HS1)
					{
						count ++;
						cap->m_durations[nlast] = tend - cap->m_offsets[nlast];

						if(count == 3)
							state = STATE_HS_SYNC_2;
					}

					else
						state = STATE_HS_SYNC_0;
				}
				break;	//end STATE_HS_SYNC_1

			//Expect a single HS-0
			case STATE_HS_SYNC_2:
				if(clock_toggling)
				{
					if(cur_data.m_type == DPhySymbol::STATE_HS0)
					{
						cap->m_durations[nlast] = tend - cap->m_offsets[nlast];
						state = STATE_HS_SYNC_3;
					}

					else
						state = STATE_HS_SYNC_0;
				}
				break;	//end STATE_HS_SYNC_2

			//Expect a single HS-1
			case STATE_HS_SYNC_3:
				if(clock_toggling)
				{
					if(cur_data.m_type == DPhySymbol::STATE_HS1)
					{
						cap->m_durations[nlast] = tclkstart - cap->m_offsets[nlast];
						count = 0;
						tstart = tclkstart;
						cur_byte = 0;
						state = STATE_HS_DATA;
					}

					else
						state = STATE_HS_SYNC_0;
				}
				break;	//end STATE_HS_SYNC_2

			//Read data bytes, LSB first
			case STATE_HS_DATA:

				if(clock_toggling)
				{
					//HS data bit
					if( (cur_data.m_type == DPhySymbol::STATE_HS0) || (cur_data.m_type == DPhySymbol::STATE_HS1) )
					{
						cur_byte >>= 1;
						if(cur_data.m_type == DPhySymbol::STATE_HS1)
							cur_byte |= 0x80;

						count ++;

						if(count == 8)
						{
							cap->m_offsets.push_back(tstart);
							cap->m_durations.push_back(tclkstart - tstart);
							cap->m_samples.push_back(DPhyDataSymbol(DPhyDataSymbol::TYPE_HS_DATA, cur_byte));
							tstart = tclkstart;
							cur_byte = 0;
							count = 0;
						}
					}

					//End of packet
					else if(cur_data.m_type == DPhySymbol::STATE_LP11)
						state = STATE_IDLE;

					//Something illegal
					else
					{
						cap->m_offsets.push_back(data->m_offsets[idata]);
						cap->m_durations.push_back(data->m_durations[idata]);
						cap->m_samples.push_back(DPhyDataSymbol(DPhyDataSymbol::TYPE_ERROR));
						state = STATE_UNKNOWN;
					}

				}

				break;	//end STATE_HS_DATA

			default:
				break;
		}

		//All good, move on
		timestamp = next_timestamp;
		AdvanceToTimestamp(clk, iclk, clklen, timestamp);
		AdvanceToTimestamp(data, idata, datalen, timestamp);
	}

	SetData(cap, 0);
}

Gdk::Color DPhyDataDecoder::GetColor(int i)
{
	auto capture = dynamic_cast<DPhyDataWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const DPhyDataSymbol& s = capture->m_samples[i];

		switch(s.m_type)
		{
			case DPhyDataSymbol::TYPE_SOT:
				return m_standardColors[COLOR_PREAMBLE];

			case DPhyDataSymbol::TYPE_HS_DATA:
				return m_standardColors[COLOR_DATA];

			case DPhyDataSymbol::TYPE_ERROR:
			default:
				return m_standardColors[COLOR_ERROR];
		}
	}

	return m_standardColors[COLOR_ERROR];
}

string DPhyDataDecoder::GetText(int i)
{
	auto capture = dynamic_cast<DPhyDataWaveform*>(GetData(0));
	char tmp[32];

	if(capture != NULL)
	{
		const DPhyDataSymbol& s = capture->m_samples[i];

		switch(s.m_type)
		{
			case DPhyDataSymbol::TYPE_SOT:
				return "SOT";

			case DPhyDataSymbol::TYPE_HS_DATA:
				snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
				return tmp;

			case DPhyDataSymbol::TYPE_ERROR:
			default:
				return "ERROR";
		}
	}

	return "ERROR";
}

