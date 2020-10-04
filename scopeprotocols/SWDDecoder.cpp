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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Implementation of SWDDecoder
 */

#include "../scopehal/scopehal.h"
#include "SWDDecoder.h"
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SWDDecoder::SWDDecoder(string color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_BUS)
{
	CreateInput("SWCLK");
	CreateInput("SWDIO");

	m_readTurnaround = "Read Turnaround Cycles";
	m_parameters[m_readTurnaround] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_readTurnaround].SetIntVal(4);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool SWDDecoder::NeedsConfig()
{
	return true;
}

bool SWDDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if(
		(i < 2) &&
		(stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) &&
		(stream.m_channel->GetWidth() == 1)
		)
	{
		return true;
	}

	return false;
}

string SWDDecoder::GetProtocolName()
{
	return "SWD";
}

void SWDDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "SWD(%s)",	GetInputDisplayName(1).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void SWDDecoder::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto clk = GetDigitalInputWaveform(0);
	auto data = GetDigitalInputWaveform(1);

	int read_turn = m_parameters[m_readTurnaround].GetIntVal();

	//Create the capture
	auto cap = new SWDWaveform;
	cap->m_timescale = 1;
	cap->m_startTimestamp = clk->m_startTimestamp;
	cap->m_startPicoseconds = clk->m_startPicoseconds;

	//Sample SWDIO on SWCLK edges
	DigitalWaveform samples;
	SampleOnRisingEdges(data, clk, samples);

	//Loop over the data and look for transactions
	enum
	{
		STATE_IDLE,
		STATE_AP_DP,
		STATE_R_W,
		STATE_ADDRESS,
		STATE_ADDR_PARITY,
		STATE_STOP,
		STATE_PARK,
		STATE_TURNAROUND,
		STATE_ACK,
		STATE_WRITE_TURNAROUND,
		STATE_DATA,
		STATE_DATA_PARITY,
		STATE_READ_TURNAROUND
	} state = STATE_IDLE;

	uint32_t	current_word	= 0;
	uint8_t		bitcount 		= 0;
	int64_t		tstart			= 0;
	bool		writing			= 0;

	size_t len = samples.m_samples.size();
	Unit ps(Unit::UNIT_PS);
	int64_t last_dur = 0;
	for(size_t i=0; i<len; i++)
	{
		//Offset sample from the clock so it's aligned to the data
		int64_t dur = samples.m_durations[i];
		int64_t off = samples.m_offsets[i];// - dur/2;

		switch(state)
		{
			case STATE_IDLE:

				if(samples.m_samples[i])
				{
					state = STATE_AP_DP;

					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					tstart = off+dur;
					cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_START, 0));
				}

				//ignore clocks with SWDIO at 0

				break;

			case STATE_AP_DP:
				state = STATE_R_W;
				cap->m_offsets.push_back(tstart);
				cap->m_durations.push_back(dur);
				tstart += dur;
				cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_AP_NDP, samples.m_samples[i]));
				break;

			case STATE_R_W:
				state = STATE_ADDRESS;

				cap->m_offsets.push_back(tstart);
				cap->m_durations.push_back(dur);
				tstart += dur;
				cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_R_NW, samples.m_samples[i]));

				current_word = 0;
				bitcount = 0;
				tstart = off + dur;

				//need to remember read vs write for later
				//so we know whether to have a turnaround between ACK and data
				writing = !samples.m_samples[i];
				break;

			case STATE_ADDRESS:

				//read LSB first data
				current_word >>= 1;
				if(samples.m_samples[i])
					current_word |= 0x80000000;
				bitcount ++;

				if(bitcount == 2)
				{
					cap->m_offsets.push_back(tstart);
					cap->m_durations.push_back((off+dur)-tstart);
					cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_ADDRESS, current_word >> 28));

					state = STATE_ADDR_PARITY;

					tstart = off+dur;
				}

				break;

			case STATE_ADDR_PARITY:
				//TODO: test parity

				state = STATE_STOP;
				cap->m_offsets.push_back(tstart);
				cap->m_durations.push_back(dur);
				tstart += dur;
				cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_PARITY_OK, samples.m_samples[i]));
				break;

			case STATE_STOP:
				state = STATE_PARK;

				cap->m_offsets.push_back(tstart);
				cap->m_durations.push_back(dur);
				tstart += dur;

				//Stop bit should be a 0
				if(!samples.m_samples[i])
					cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_STOP, 0));
				else
					cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_ERROR, 0));
				break;

			case STATE_PARK:
				state = STATE_TURNAROUND;
				cap->m_offsets.push_back(tstart);
				cap->m_durations.push_back(dur);
				tstart += dur;
				cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_PARK, samples.m_samples[i]));
				break;

			case STATE_TURNAROUND:
				state = STATE_ACK;
				cap->m_offsets.push_back(tstart);
				cap->m_durations.push_back(dur);
				tstart += dur;
				cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_TURNAROUND, samples.m_samples[i]));

				current_word = 0;
				bitcount = 0;
				break;

			case STATE_ACK:
				//read LSB first data
				current_word >>= 1;
				if(samples.m_samples[i])
					current_word |= 0x80000000;
				bitcount ++;

				if(bitcount == 3)
				{
					cap->m_offsets.push_back(tstart);
					cap->m_durations.push_back((off+dur)-tstart);
					cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_ACK, current_word >> 29));

					if(writing)
						state = STATE_WRITE_TURNAROUND;
					else
						state = STATE_DATA;

					tstart = off+dur;
				}
				break;

			case STATE_WRITE_TURNAROUND:
				state = STATE_DATA;
				cap->m_offsets.push_back(tstart);
				cap->m_durations.push_back(dur);
				tstart += dur;
				cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_TURNAROUND, samples.m_samples[i]));

				current_word = 0;
				bitcount = 0;
				break;

			case STATE_DATA:
				//read LSB first data
				current_word >>= 1;
				if(samples.m_samples[i])
					current_word |= 0x80000000;
				bitcount ++;

				if(bitcount == 32)
				{
					cap->m_offsets.push_back(tstart);
					cap->m_durations.push_back((off+dur)-tstart);
					cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_DATA, current_word));

					state = STATE_DATA_PARITY;

					tstart = off+dur;
				}
				break;

			case STATE_DATA_PARITY:
				//TODO: test parity
				cap->m_offsets.push_back(tstart);
				cap->m_durations.push_back(min(dur, last_dur));	//clock may stop between packets, don't extend sample
				cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_PARITY_OK, samples.m_samples[i]));
				tstart += dur;

				if(!writing)
				{
					bitcount = 0;
					state = STATE_READ_TURNAROUND;
				}
				else
					state = STATE_IDLE;
				break;

			case STATE_READ_TURNAROUND:
				bitcount ++;

				if(bitcount == read_turn)
				{
					state = STATE_IDLE;

					cap->m_offsets.push_back(tstart);
					cap->m_durations.push_back(last_dur);
					cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_TURNAROUND, samples.m_samples[i]));
				}
				break;
		}

		last_dur = dur;
	}
	SetData(cap, 0);
}

Gdk::Color SWDDecoder::GetColor(int i)
{
	auto capture = dynamic_cast<SWDWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const SWDSymbol& s = capture->m_samples[i];

		switch(s.m_stype)
		{
			case SWDSymbol::TYPE_START:
			case SWDSymbol::TYPE_STOP:
			case SWDSymbol::TYPE_PARK:
			case SWDSymbol::TYPE_TURNAROUND:
				return m_standardColors[COLOR_PREAMBLE];

			case SWDSymbol::TYPE_AP_NDP:
			case SWDSymbol::TYPE_R_NW:
				return m_standardColors[COLOR_CONTROL];

			case SWDSymbol::TYPE_ACK:
				switch(s.m_data)
				{
					case 1:
					case 2:
						return m_standardColors[COLOR_CONTROL];

					case 4:
					default:
						return m_standardColors[COLOR_ERROR];
				}

			case SWDSymbol::TYPE_ADDRESS:
				return m_standardColors[COLOR_ADDRESS];

			case SWDSymbol::TYPE_PARITY_OK:
				return m_standardColors[COLOR_CHECKSUM_OK];
			case SWDSymbol::TYPE_PARITY_BAD:
				return m_standardColors[COLOR_CHECKSUM_BAD];

			case SWDSymbol::TYPE_DATA:
				return m_standardColors[COLOR_DATA];

			case SWDSymbol::TYPE_ERROR:
			default:
				return m_standardColors[COLOR_ERROR];
		}
	}
	return m_standardColors[COLOR_ERROR];
}

string SWDDecoder::GetText(int i)
{
	auto capture = dynamic_cast<SWDWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const SWDSymbol& s = capture->m_samples[i];
		char tmp[32];

		switch(s.m_stype)
		{
			case SWDSymbol::TYPE_START:
				return "START";

			case SWDSymbol::TYPE_AP_NDP:
				if(s.m_data)
					return "AP";
				else
					return "DP";

			case SWDSymbol::TYPE_R_NW:
				if(s.m_data)
					return "R";
				else
					return "W";

			case SWDSymbol::TYPE_ADDRESS:
				snprintf(tmp, sizeof(tmp), "Reg %02x", s.m_data);
				return string(tmp);

			case SWDSymbol::TYPE_PARITY_OK:
				return "OK";

			case SWDSymbol::TYPE_STOP:
				return "STOP";
			case SWDSymbol::TYPE_PARK:
				return "PARK";

			case SWDSymbol::TYPE_TURNAROUND:
				return "TURN";

			case SWDSymbol::TYPE_ACK:
				switch(s.m_data)
				{
					case 1:
						return "ACK";
					case 2:
						return "WAIT";
					case 4:
						return "FAULT";
					default:
						return "ERROR";
				}
				break;

			case SWDSymbol::TYPE_DATA:
				snprintf(tmp, sizeof(tmp), "%08x", s.m_data);
				return string(tmp);

			case SWDSymbol::TYPE_ERROR:
			case SWDSymbol::TYPE_PARITY_BAD:
			default:
				return "ERROR";
		}
	}
	return "";
}
