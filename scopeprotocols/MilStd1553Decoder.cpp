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
#include "MilStd1553Decoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

MilStd1553Decoder::MilStd1553Decoder(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_BUS)
{
	CreateInput("in");
}

MilStd1553Decoder::~MilStd1553Decoder()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string MilStd1553Decoder::GetProtocolName()
{
	return "MIL-STD-1553";
}

bool MilStd1553Decoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG))
		return true;

	return false;
}

void MilStd1553Decoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "MIL-STD-1553(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

bool MilStd1553Decoder::NeedsConfig()
{
	//Everything is specified by the protocol, nothing to configure
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void MilStd1553Decoder::Refresh()
{
	Unit fs(Unit::UNIT_FS);

	//Get the input data
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	auto din = GetAnalogInputWaveform(0);
	size_t len = din->m_samples.size();

	//Copy our time scales from the input
	auto cap = new MilStd1553Waveform;
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	cap->m_triggerPhase = din->m_triggerPhase;
	SetData(cap, 0);

	enum
	{
		STATE_IDLE,
		STATE_SYNC_COMMAND_HIGH,
		STATE_SYNC_COMMAND_LOW,
		STATE_SYNC_DATA_LOW,
		STATE_SYNC_DATA_HIGH,

		STATE_DATA_0_LOW,
		STATE_DATA_0_HIGH,

		STATE_DATA_1_HIGH,
		STATE_DATA_1_LOW,

		STATE_TURNAROUND,

		//TODO
		STATE_HANG

	} state = STATE_IDLE;

	enum
	{
		FRAME_STATE_IDLE,

		FRAME_STATE_HANG
	} frame_state = FRAME_STATE_IDLE;

	//Logic high/low thresholds (anything between is considered undefined)
	const float high = 2;
	const float low = -2;

	//Nominal duration of various protocol elements
	int64_t sync_len_fs			= 1500 * 1000 * 1000;
	int64_t data_len_fs			= 500 * 1000 * 1000;
	int64_t ifg_len_fs			= 4000 * 1000 * 1000L;

	int64_t sync_data_threshold	= (sync_len_fs*2 + data_len_fs/2) / din->m_timescale;
	int64_t data_len_threshold	= (data_len_fs*2 + data_len_fs/2) / din->m_timescale;
	int64_t sync_len_samples	= sync_len_fs / din->m_timescale;
	int64_t data_len_samples	= data_len_fs / din->m_timescale;
	int64_t ifg_len_samples		= ifg_len_fs / din->m_timescale;

	LogDebug("Start decode\n");
	LogIndenter li;

	bool last_bit = false;
	int64_t tbitstart = 0;
	vector<int64_t> bitstarts;
	int bitcount = 0;
	uint16_t word = 0;
	for(size_t i=0; i<len; i++)
	{
		int64_t timestamp = din->m_offsets[i];
		int64_t duration = timestamp - tbitstart;

		//Determine the current line state
		bool current_bit = last_bit;
		bool valid = false;
		if(din->m_samples[i] > high)
		{
			current_bit = true;
			valid = true;
		}
		else if(din->m_samples[i] < low)
		{
			current_bit = false;
			valid = true;
		}

		//Words time out after the parity bit completes
		bool word_valid = false;
		if( (bitcount == 16) && (duration >= 2*data_len_samples) )
		{
			bitstarts.push_back(tbitstart);
			word <<= 1;
			bitcount ++;

			if(state == STATE_DATA_1_LOW)
				word |= 1;

			word_valid = true;
			tbitstart += data_len_samples*2;
			state = STATE_TURNAROUND;
		}

		//Valid data level
		else if(valid)
		{
			////////////////////////////////////////////////////////////////////////////////////////////////////////////
			// Low level state machine (turn bits into words)

			switch(state)
			{
				////////////////////////////////////////////////////////////////////////////////////////////////////////
				// Idle, nothing to do

				case STATE_IDLE:

					//Start a sync pulse
					if(current_bit)
						state = STATE_SYNC_COMMAND_HIGH;
					else
						state = STATE_SYNC_DATA_LOW;

					tbitstart = timestamp;

					break;	//end STATE_IDLE

				////////////////////////////////////////////////////////////////////////////////////////////////////////
				// Start of a command/status word

				//First half of command pulse
				case STATE_SYNC_COMMAND_HIGH:

					//Look for falling edge
					if(!current_bit)
					{
						//Complain if start bit isn't reasonably close to the nominal duration
						float fdur = duration * 1.0 / sync_len_samples;
						if( (fdur > 1.05) || (fdur < 0.95) )
						{
							cap->m_offsets.push_back(tbitstart);
							cap->m_durations.push_back(duration);
							cap->m_samples.push_back(MilStd1553Symbol(MilStd1553Symbol::TYPE_ERROR, 0));
							state = STATE_IDLE;
						}

						//All good, wait for second half
						else
							state = STATE_SYNC_COMMAND_LOW;
					}

					break;	//end STATE_SYNC_COMMAND_HIGH

				//Second half of command pulse
				case STATE_SYNC_COMMAND_LOW:

					//Look for rising edge
					if(current_bit)
					{
						//Command pulse is 1-0.
						//If the first data bit is a logic 0, it's a 0-1 sequence so we should see a longer-than-normal
						//low period.
						if(duration > sync_data_threshold)
						{
							//Save the sync pulse
							cap->m_offsets.push_back(tbitstart);
							cap->m_durations.push_back(sync_len_samples*2);
							cap->m_samples.push_back(MilStd1553Symbol(MilStd1553Symbol::TYPE_SYNC_CTRL_STAT, 0));

							//New data bit starts right when the sync pulse ends
							tbitstart += sync_len_samples*2;

							//We're now in the second half of a logic-0 bit
							state = STATE_DATA_0_HIGH;
						}

						//First data bit is a logic 1 (1-0 sequence)
						else
						{
							cap->m_offsets.push_back(tbitstart);
							cap->m_durations.push_back(duration);
							cap->m_samples.push_back(MilStd1553Symbol(MilStd1553Symbol::TYPE_SYNC_CTRL_STAT, 0));

							tbitstart = timestamp;
							state = STATE_DATA_1_HIGH;
						}

						//Begin the data word
						bitcount = 0;
						word = 0;

					}	//end STATE_SYNC_COMMAND_LOW

					break;

				////////////////////////////////////////////////////////////////////////////////////////////////////////
				// Start of a data word

				case STATE_SYNC_DATA_LOW:
					break;	//end STATE_SYNC_DATA_LOW

				////////////////////////////////////////////////////////////////////////////////////////////////////////
				// Data bits

				//Just wait for data to go high
				case STATE_DATA_0_LOW:
					if(current_bit)
						state = STATE_DATA_0_HIGH;
					break;	//end STATE_DATA_0_LOW

				case STATE_DATA_0_HIGH:
					if(!current_bit)
					{
						//Save the data bit
						bitstarts.push_back(tbitstart);
						word <<= 1;
						bitcount ++;

						//End of data word?
						if(bitcount == 17)
						{
							word_valid = true;
							tbitstart += data_len_samples*2;
							state = STATE_TURNAROUND;
						}

						//0-1 + 1-0 = logic 01
						else if(duration > data_len_threshold)
						{
							tbitstart += data_len_samples*2;
							state = STATE_DATA_1_LOW;
						}

						//0-1 + 0-1 = logic 00
						else
						{
							tbitstart = timestamp;
							state = STATE_DATA_0_LOW;
						}

					}
					break;	//end STATE_DATA_0_HIGH

				//Just wait for data to go low
				case STATE_DATA_1_HIGH:
					if(!current_bit)
						state = STATE_DATA_1_LOW;
					break;	//end STATE_DATA_1_HIGH

				case STATE_DATA_1_LOW:
					if(current_bit)
					{
						//Save the data bit
						bitstarts.push_back(tbitstart);
						word = (word << 1) | 1;
						bitcount ++;

						//End of data word?
						if(bitcount == 17)
						{
							word_valid = true;
							tbitstart += data_len_samples*2;
							state = STATE_TURNAROUND;
						}

						//1-0 + 0-1 = logic 10
						else if(duration > data_len_threshold)
						{
							tbitstart += data_len_samples*2;
							state = STATE_DATA_0_HIGH;
						}

						//1-0 + 1-0 = logic 11
						else
						{
							tbitstart = timestamp;
							state = STATE_DATA_1_HIGH;
						}

					}
					break;	//end STATE_DATA_1_LOW

				////////////////////////////////////////////////////////////////////////////////////////////////////////
				// Bus turnaround, wait for an edge after the timeout period

				case STATE_TURNAROUND:

					//Ignore everything until the minimum inter-frame gap of 4us
					//TODO: display timeouts if nothing after 14us after a read?
					if(duration > ifg_len_samples)
					{
						//Add the IFG sample
						cap->m_offsets.push_back(tbitstart);
						cap->m_durations.push_back(duration);
						cap->m_samples.push_back(MilStd1553Symbol(MilStd1553Symbol::TYPE_TURNAROUND, 0));

						if(current_bit)
							state = STATE_SYNC_COMMAND_HIGH;
						else
							state = STATE_SYNC_DATA_LOW;

						tbitstart = timestamp;
					}

					break;	//end STATE_TURNAROUND
			}
		}

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Upper level protocol logic

		if(word_valid)
		{
			bool parity = (word & 1);
			word >>= 1;

			bool expected_parity = true;
			for(int j=0; j<16; j++)
			{
				if( (word >> j) & 1)
					expected_parity = !expected_parity;
			}

			switch(frame_state)
			{
				case FRAME_STATE_IDLE:

					//First 5 bits are RT address
					cap->m_offsets.push_back(bitstarts[0]);
					cap->m_durations.push_back(bitstarts[6] - bitstarts[0]);
					cap->m_samples.push_back(MilStd1553Symbol(MilStd1553Symbol::TYPE_RT_ADDR, (word >> 11) & 0x1f ));

					//6th bit is 1 for RT->BC and 0 for BC->RT
					cap->m_offsets.push_back(bitstarts[6]);
					cap->m_durations.push_back(bitstarts[7] - bitstarts[6]);
					cap->m_samples.push_back(MilStd1553Symbol(MilStd1553Symbol::TYPE_DIRECTION, (word >> 10) & 0x1 ));

					//Next 5 bits are sub-address
					cap->m_offsets.push_back(bitstarts[7]);
					cap->m_durations.push_back(bitstarts[11] - bitstarts[7]);
					cap->m_samples.push_back(MilStd1553Symbol(MilStd1553Symbol::TYPE_SUB_ADDR, (word >> 5) & 0x1f ));

					//Last 5 are data length
					cap->m_offsets.push_back(bitstarts[11]);
					cap->m_durations.push_back(bitstarts[16] - bitstarts[11]);
					cap->m_samples.push_back(MilStd1553Symbol(MilStd1553Symbol::TYPE_LENGTH, word & 0x1f));

					//Parity bit
					cap->m_offsets.push_back(bitstarts[16]);
					cap->m_durations.push_back(timestamp - bitstarts[16]);
					if(expected_parity == parity)
						cap->m_samples.push_back(MilStd1553Symbol(MilStd1553Symbol::TYPE_PARITY_OK, parity));
					else
						cap->m_samples.push_back(MilStd1553Symbol(MilStd1553Symbol::TYPE_PARITY_BAD, parity));

					frame_state = FRAME_STATE_HANG;

					break;	//end FRAME_STATE_IDLE

				case FRAME_STATE_HANG:
					break;
			}

			//Clear out word state
			bitstarts.clear();
			word = 0;
			bitcount = 0;
		}

		last_bit = current_bit;
	}
}

Gdk::Color MilStd1553Decoder::GetColor(int i)
{
	auto capture = dynamic_cast<MilStd1553Waveform*>(GetData(0));
	if(capture != NULL)
	{
		const MilStd1553Symbol& s = capture->m_samples[i];
		switch(s.m_stype)
		{
			case MilStd1553Symbol::TYPE_SYNC_CTRL_STAT:
			case MilStd1553Symbol::TYPE_SYNC_DATA:
			case MilStd1553Symbol::TYPE_TURNAROUND:
				return m_standardColors[COLOR_PREAMBLE];

			case MilStd1553Symbol::TYPE_RT_ADDR:
			case MilStd1553Symbol::TYPE_SUB_ADDR:
				return m_standardColors[COLOR_ADDRESS];

			case MilStd1553Symbol::TYPE_DIRECTION:
			case MilStd1553Symbol::TYPE_LENGTH:
				return m_standardColors[COLOR_CONTROL];

			//case MilStd1553Symbol::TYPE_DATA:
			//	return m_standardColors[COLOR_DATA];

			case MilStd1553Symbol::TYPE_PARITY_OK:
				return m_standardColors[COLOR_CHECKSUM_OK];

			case MilStd1553Symbol::TYPE_PARITY_BAD:
			case MilStd1553Symbol::TYPE_ERROR:
			default:
				return m_standardColors[COLOR_ERROR];
		}
	}

	return m_standardColors[COLOR_ERROR];
}

string MilStd1553Decoder::GetText(int i)
{
	char tmp[128] = "";
	auto capture = dynamic_cast<MilStd1553Waveform*>(GetData(0));
	if(capture != NULL)
	{
		const MilStd1553Symbol& s = capture->m_samples[i];
		switch(s.m_stype)
		{
			case MilStd1553Symbol::TYPE_SYNC_CTRL_STAT:
				return "Sync: Ctl/Stat";

			case MilStd1553Symbol::TYPE_SYNC_DATA:
				return "Sync: Data";

			case MilStd1553Symbol::TYPE_RT_ADDR:
				snprintf(tmp, sizeof(tmp), "RT %d", s.m_data);
				return tmp;

			case MilStd1553Symbol::TYPE_SUB_ADDR:
				snprintf(tmp, sizeof(tmp), "SA %d", s.m_data);
				return tmp;

			case MilStd1553Symbol::TYPE_DIRECTION:
				if(s.m_data)
					return "RT to BC";
				else
					return "BC to RT";

			case MilStd1553Symbol::TYPE_LENGTH:
				if(s.m_data == 0)
					return "Len: 32";
				else
					return string("Len: ") + to_string(s.m_data);

			case MilStd1553Symbol::TYPE_PARITY_BAD:
			case MilStd1553Symbol::TYPE_PARITY_OK:
				return string("Parity: ") + to_string(s.m_data);

			case MilStd1553Symbol::TYPE_TURNAROUND:
				return "Turnaround";


			/*
			case MilStd1553Symbol::TYPE_DATA:
				snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
				return tmp;
			*/

			case MilStd1553Symbol::TYPE_ERROR:
			default:
				return "ERROR";
		}
	}
	return "";
}
