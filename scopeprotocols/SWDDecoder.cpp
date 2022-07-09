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
	@brief Implementation of SWDDecoder
 */

#include "../scopehal/scopehal.h"
#include "SWDDecoder.h"
#include <algorithm>

using namespace std;

// Magic numbers for the SWD protocol
const uint16_t SWDDecoder::c_JTAG_TO_SWD_SEQ = 0xE79E;		 // Switch from JTAG to SWD from LineReset state
const uint16_t SWDDecoder::c_SWD_TO_JTAG_SEQ = 0xE73C;		 // Switch from SWD toJTAG from LineReset state
const uint16_t SWDDecoder::c_SWD_TO_DORMANT_SEQ = 0xE3BC;	 // Switch to Dormant state from LineReset state
const uint32_t SWDDecoder::c_magic_seqlen = 16;				 // Length of a magic (state switch) sequence in bits
const uint32_t SWDDecoder::c_magic_wakeuplen = 128;			 // Length of a magic wakeup from dormant
const uint32_t SWDDecoder::c_reset_minseqlen = 50;			 // Minimum number of 1's before its a line reset

// The dormant wakeup magic sequence
const uint8_t SWDDecoder::c_wakeup[16] = {
	0x19, 0xBC, 0x0E, 0xA2, 0xE3, 0xDD, 0xAF, 0xE9, 0x86, 0x85, 0x2D, 0x95, 0x62, 0x09, 0xF3, 0x92};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SWDDecoder::SWDDecoder(const string& color) : Filter(color, CAT_BUS)
{
	AddProtocolStream("data");
	CreateInput("SWCLK");
	CreateInput("SWDIO");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool SWDDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if((i < 2) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
	{
		return true;
	}

	return false;
}

string SWDDecoder::GetProtocolName()
{
	return "SWD";
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

	//Create the capture
	auto cap = new SWDWaveform;
	cap->m_timescale = 1;
	cap->m_startTimestamp = clk->m_startTimestamp;
	cap->m_startFemtoseconds = clk->m_startFemtoseconds;

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

	uint32_t current_word = 0;
	uint8_t bitcount = 0;
	int64_t tstart = 0;
	bool writing = 0;
	uint32_t ticks_to_zero = 0;

	size_t len = samples.m_samples.size();
	int64_t last_dur = 0;
	int64_t dur;
	int64_t off;
	int32_t parity = 0;

	for(size_t i = 0; i < len; i++)
	{
		//Offset sample from the clock so it's aligned to the data
		dur = samples.m_durations[i];
		off = samples.m_offsets[i] - dur / 2;

		// Scan forward through data looking for a line reset
		if(!ticks_to_zero)
		{
			uint64_t stateLen = 0;
			while((samples.m_samples[i + ticks_to_zero]) && (i + ticks_to_zero < len))
			{
				stateLen += samples.m_durations[i + ticks_to_zero];
				ticks_to_zero++;
			}

			if(ticks_to_zero >= c_reset_minseqlen)
			{
				// Yep, this is a line reset, label it as such
				cap->m_offsets.push_back(off);
				cap->m_durations.push_back(stateLen);
				tstart = off + dur;
				cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_LINERESET, 0));
				state = STATE_IDLE;
				i += ticks_to_zero;
				ticks_to_zero = 0;

				// After a reset there can be a mode-change, so check for that
				dur = samples.m_durations[i];
				off = samples.m_offsets[i] - dur / 2;
				current_word = 0;
				stateLen = 0;
				for(uint32_t it = 0; it < c_magic_seqlen; it++)
				{
					current_word = (current_word >> 1) | (samples.m_samples[i + it] ? (1 << (c_magic_seqlen - 1)) : 0);
					stateLen += samples.m_durations[i + it];
				}

				if((current_word == c_JTAG_TO_SWD_SEQ) || (current_word == c_SWD_TO_JTAG_SEQ) ||
					(current_word == c_SWD_TO_DORMANT_SEQ))
				{
					// This is a line state change
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(stateLen);
					tstart = off + dur;
					i += c_magic_seqlen - 1;
					dur = samples.m_durations[i];
					off = samples.m_offsets[i] - dur / 2;

					switch(current_word)
					{
						case c_JTAG_TO_SWD_SEQ:
							cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_JTAGTOSWD, 0));
							break;

						case c_SWD_TO_JTAG_SEQ:
							cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_SWDTOJTAG, 0));
							break;

						case c_SWD_TO_DORMANT_SEQ:
							cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_SWDTODORMANT, 0));
							break;

						default:
							break;
					}
				}

				continue;
			}
		}
		else
			ticks_to_zero--;

		// Finally, check we're not being pulled out of dormant mode...
		// just slide along the wakeup sequence and see if we make it to the other end
		uint32_t dindex = 0;
		while((dindex < c_magic_wakeuplen) &&
			  samples.m_samples[i + dindex] == (((c_wakeup[dindex / 8]) & (1 << (dindex % 8))) != 0))
			dindex++;

		if(dindex == c_magic_wakeuplen)
		{
			// This _is_ a wakeup sequence, label it
			cap->m_offsets.push_back(off);
			cap->m_durations.push_back(dur * c_magic_wakeuplen);
			tstart = off + dur;
			cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_LEAVEDORMANT, 0));
			state = STATE_IDLE;
			i += c_magic_wakeuplen;
			ticks_to_zero = 0;
			continue;
		}

		switch(state)
		{
			case STATE_IDLE:

				if(samples.m_samples[i])
				{
					state = STATE_AP_DP;

					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					tstart = off + dur;
					parity = 0;
					cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_START, 0));
				}

				//ignore clocks with SWDIO at 0
				break;

			case STATE_AP_DP:
				state = STATE_R_W;
				cap->m_offsets.push_back(tstart);
				cap->m_durations.push_back(dur);
				tstart += dur;
				parity = samples.m_samples[i] ? !parity : parity;
				cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_AP_NDP, samples.m_samples[i]));
				break;

			case STATE_R_W:
				state = STATE_ADDRESS;

				cap->m_offsets.push_back(tstart);
				cap->m_durations.push_back(dur);
				parity = samples.m_samples[i] ? !parity : parity;
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
				parity = samples.m_samples[i] ? !parity : parity;
				if(samples.m_samples[i])
					current_word |= 0x80000000;
				bitcount++;

				if(bitcount == 2)
				{
					cap->m_offsets.push_back(tstart);
					cap->m_durations.push_back((off + dur) - tstart);
					cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_ADDRESS, current_word >> 28));

					state = STATE_ADDR_PARITY;

					tstart = off + dur;
				}

				break;

			case STATE_ADDR_PARITY:
				state = STATE_STOP;
				cap->m_offsets.push_back(tstart);
				cap->m_durations.push_back(dur);
				tstart += dur;
				if(samples.m_samples[i] == parity)
					cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_PARITY_OK, samples.m_samples[i]));
				else
					cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_PARITY_BAD, samples.m_samples[i]));
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
				bitcount++;

				if(bitcount == 3)
				{
					parity = 0;
					cap->m_offsets.push_back(tstart);
					cap->m_durations.push_back((off + dur) - tstart);
					cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_ACK, current_word >> 29));

					// Only proceed to reading or writing phase if we got an 'OK' response
					// Otherwise line gets turned around for writing again
					if((current_word >> 29) != 1)
						state = STATE_READ_TURNAROUND;
					else if(writing)
						state = STATE_WRITE_TURNAROUND;
					else
						state = STATE_DATA;

					tstart = off + dur;
					bitcount = 0;
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
				parity = samples.m_samples[i] ? !parity : parity;
				if(samples.m_samples[i])
					current_word |= 0x80000000;
				bitcount++;

				if(bitcount == 32)
				{
					cap->m_offsets.push_back(tstart);
					cap->m_durations.push_back((off + dur) - tstart);
					cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_DATA, current_word));

					state = STATE_DATA_PARITY;

					tstart = off + dur;
				}
				break;

			case STATE_DATA_PARITY:
				cap->m_offsets.push_back(tstart);
				cap->m_durations.push_back(min(dur, last_dur));	   //clock may stop between packets, don't extend sample

				if(samples.m_samples[i] == parity)
					cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_PARITY_OK, samples.m_samples[i]));
				else
					cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_PARITY_BAD, samples.m_samples[i]));
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
				state = STATE_IDLE;

				cap->m_offsets.push_back(tstart);
				cap->m_durations.push_back(last_dur);
				cap->m_samples.push_back(SWDSymbol(SWDSymbol::TYPE_TURNAROUND, samples.m_samples[i]));
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
			case SWDSymbol::TYPE_LINERESET:
				return m_standardColors[COLOR_PREAMBLE];

			case SWDSymbol::TYPE_SWDTOJTAG:
			case SWDSymbol::TYPE_JTAGTOSWD:
			case SWDSymbol::TYPE_SWDTODORMANT:
			case SWDSymbol::TYPE_LEAVEDORMANT:
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
			case SWDSymbol::TYPE_LINERESET:
				return "LINE RESET";

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

			case SWDSymbol::TYPE_PARITY_BAD:
				return "BAD";

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

			case SWDSymbol::TYPE_SWDTOJTAG:
				return "SWD TO JTAG";

			case SWDSymbol::TYPE_JTAGTOSWD:
				return "JTAG TO SWD";

			case SWDSymbol::TYPE_SWDTODORMANT:
				return "SWD TO DORMANT";

			case SWDSymbol::TYPE_LEAVEDORMANT:
				return "LEAVE DORMANT";

			case SWDSymbol::TYPE_ERROR:
			default:
				return "ERROR";
		}
	}
	return "";
}
