/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2021 Andrew D. Zonenberg and contributors                                                         *
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
	: PacketDecoder(color, CAT_BUS)
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

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG))
		return true;

	return false;
}

vector<string> MilStd1553Decoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("Direction");
	ret.push_back("RT");
	ret.push_back("SA");
	ret.push_back("Status");
	ret.push_back("Len");
	return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void MilStd1553Decoder::Refresh()
{
	ClearPackets();

	//Get the input data
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	auto din = GetInputWaveform(0);
	din->PrepareForCpuAccess();
	size_t len = din->size();

	auto sdin = dynamic_cast<SparseAnalogWaveform*>(din);
	auto udin = dynamic_cast<UniformAnalogWaveform*>(din);

	//Copy our time scales from the input
	auto cap = new MilStd1553Waveform;
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	cap->m_triggerPhase = din->m_triggerPhase;
	SetData(cap, 0);
	cap->PrepareForCpuAccess();

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

		STATE_TURNAROUND
	} state = STATE_IDLE;

	enum
	{
		FRAME_STATE_IDLE,
		FRAME_STATE_STATUS,
		FRAME_STATE_DATA
	} frame_state = FRAME_STATE_IDLE;

	//Logic high/low thresholds (anything between is considered undefined)
	const float high = 2;
	const float low = -2;

	//Nominal duration of various protocol elements
	const int64_t k = 1000;

	int64_t sync_len_fs			= 1500 * k * k;
	int64_t data_len_fs			= 500 * k * k;
	int64_t ifg_len_fs			= 4000 * k * k;

	int64_t sync_data_threshold	= (sync_len_fs*2 + data_len_fs/2) / din->m_timescale;
	int64_t data_len_threshold	= (data_len_fs*2 + data_len_fs/2) / din->m_timescale;
	int64_t sync_len_samples	= sync_len_fs / din->m_timescale;
	int64_t data_len_samples	= data_len_fs / din->m_timescale;
	int64_t ifg_len_samples		= ifg_len_fs / din->m_timescale;

	bool last_bit = false;
	int64_t tbitstart = 0;
	vector<int64_t> bitstarts;
	int bitcount = 0;
	uint16_t word = 0;
	int data_word_count = 0;
	int data_words_expected = 0;
	bool ctrl_direction = false;
	Packet* pack = NULL;
	for(size_t i=0; i<len; i++)
	{
		int64_t timestamp = ::GetOffset(sdin, udin, i);
		int64_t duration = timestamp - tbitstart;

		//Determine the current line state
		bool current_bit = last_bit;
		bool valid = false;
		if(GetValue(sdin, udin, i) > high)
		{
			current_bit = true;
			valid = true;
		}
		else if(GetValue(sdin, udin, i) < low)
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
					if(!current_bit)
						state = STATE_SYNC_COMMAND_LOW;
					break;	//end STATE_SYNC_COMMAND_HIGH

				//Second half of command pulse
				case STATE_SYNC_COMMAND_LOW:
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

					}
					break;	//end STATE_SYNC_COMMAND_LOW

				////////////////////////////////////////////////////////////////////////////////////////////////////////
				// Start of a data word

				case STATE_SYNC_DATA_LOW:
					if(current_bit)
						state = STATE_SYNC_DATA_HIGH;
					break;	//end STATE_SYNC_DATA_LOW

				case STATE_SYNC_DATA_HIGH:
					if(!current_bit)
					{
						//Command pulse is 0-1
						//If the first data bit is a logic 1, it's a 1-0 sequence so we should see a longer-than-normal
						//low period.
						if(duration > sync_data_threshold)
						{
							//Save the sync pulse
							cap->m_offsets.push_back(tbitstart);
							cap->m_durations.push_back(sync_len_samples*2);
							cap->m_samples.push_back(MilStd1553Symbol(MilStd1553Symbol::TYPE_SYNC_DATA, 0));

							//New data bit starts right when the sync pulse ends
							tbitstart += sync_len_samples*2;

							//We're now in the second half of a logic-1 bit
							state = STATE_DATA_1_LOW;
						}

						//First data bit is a logic 0 (0-1 sequence)
						else
						{
							cap->m_offsets.push_back(tbitstart);
							cap->m_durations.push_back(duration);
							cap->m_samples.push_back(MilStd1553Symbol(MilStd1553Symbol::TYPE_SYNC_DATA, 0));

							tbitstart = timestamp;
							state = STATE_DATA_0_LOW;
						}

						//Begin the data word
						bitcount = 0;
						word = 0;

					}
					break;	//end STATE_SYNC_DATA_HIGH

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
			tbitstart = timestamp;

			bool expected_parity = true;
			for(int j=0; j<16; j++)
			{
				if( (word >> j) & 1)
					expected_parity = !expected_parity;
			}

			switch(frame_state)
			{
				////////////////////////////////////////////////////////////////////////////////////////////////////////
				// Wait for a transaction to start

				case FRAME_STATE_IDLE:
					{
						//Start a packet
						pack = new Packet;
						pack->m_offset = bitstarts[0] * din->m_timescale;
						m_packets.push_back(pack);

						//First 5 bits are RT address
						cap->m_offsets.push_back(bitstarts[0]);
						cap->m_durations.push_back(bitstarts[6] - bitstarts[0]);
						uint8_t rtaddr = (word >> 11) & 0x1f;
						cap->m_samples.push_back(MilStd1553Symbol(MilStd1553Symbol::TYPE_RT_ADDR, rtaddr ));
						pack->m_headers["RT"] = to_string(rtaddr);

						//6th bit is 1 for RT->BC and 0 for BC->RT
						ctrl_direction = (word >> 10) & 0x1;
						cap->m_offsets.push_back(bitstarts[6]);
						cap->m_durations.push_back(bitstarts[7] - bitstarts[6]);
						cap->m_samples.push_back(MilStd1553Symbol(MilStd1553Symbol::TYPE_DIRECTION, ctrl_direction ));
						if(ctrl_direction)
						{
							pack->m_headers["Direction"] = "RT -> BC";
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
						}
						else
						{
							pack->m_headers["Direction"] = "BC -> RT";
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
						}

						//Next 5 bits are sub-address
						cap->m_offsets.push_back(bitstarts[7]);
						uint8_t saaddr = (word >> 5) & 0x1f ;
						cap->m_durations.push_back(bitstarts[11] - bitstarts[7]);
						cap->m_samples.push_back(MilStd1553Symbol(MilStd1553Symbol::TYPE_SUB_ADDR, saaddr));
						pack->m_headers["SA"] = to_string(saaddr);

						//Last 5 are data length
						cap->m_offsets.push_back(bitstarts[11]);
						cap->m_durations.push_back(bitstarts[16] - bitstarts[11]);
						data_words_expected = word & 0x1f;
						if(data_words_expected == 0)
							data_words_expected = 32;
						cap->m_samples.push_back(MilStd1553Symbol(MilStd1553Symbol::TYPE_LENGTH, data_words_expected));
						pack->m_headers["Len"] = to_string(data_words_expected * 2);	//in bytes

						//Parity bit
						cap->m_offsets.push_back(bitstarts[16]);
						cap->m_durations.push_back(timestamp - bitstarts[16]);
						if(expected_parity == parity)
							cap->m_samples.push_back(MilStd1553Symbol(MilStd1553Symbol::TYPE_PARITY_OK, parity));
						else
						{
							cap->m_samples.push_back(MilStd1553Symbol(MilStd1553Symbol::TYPE_PARITY_BAD, parity));
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
						}

						//If this is a RT->BC frame, we're in the turnaround period now
						if(ctrl_direction)
						{
							state = STATE_TURNAROUND;
							frame_state = FRAME_STATE_STATUS;
						}

						//Otherwise, expect data words right away
						else
						{
							state = STATE_IDLE;
							frame_state = FRAME_STATE_DATA;
						}
					}

					data_word_count = 0;
					break;	//end FRAME_STATE_IDLE

				////////////////////////////////////////////////////////////////////////////////////////////////////////
				// Process a status word

				case FRAME_STATE_STATUS:

					//First 5 bits are RT address
					cap->m_offsets.push_back(bitstarts[0]);
					cap->m_durations.push_back(bitstarts[6] - bitstarts[0]);
					cap->m_samples.push_back(MilStd1553Symbol(MilStd1553Symbol::TYPE_RT_ADDR, (word >> 11) & 0x1f ));

					//6 - Message error bit
					cap->m_offsets.push_back(bitstarts[6]);
					cap->m_durations.push_back(bitstarts[7] - bitstarts[6]);
					if(word & 0x40)
						cap->m_samples.push_back(MilStd1553Symbol(MilStd1553Symbol::TYPE_MSG_ERR, 0));
					else
						cap->m_samples.push_back(MilStd1553Symbol(MilStd1553Symbol::TYPE_MSG_OK, 0));

					{
						int status = 0;
						string sstat = "";

						//7 - instrumentation bit, always zero
						if(status & 0x0200)
						{
							status |= MilStd1553Symbol::STATUS_MALFORMED;
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
						}

						//8 - service request
						if(status & 0x0100)
						{
							status |= MilStd1553Symbol::STATUS_SERVICE_REQUEST;
							sstat += "SrvReq ";
						}

						//9-11 = reserved, always zero
						if(status & 0x00e0)
						{
							status |= MilStd1553Symbol::STATUS_MALFORMED;
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
						}

						//12 = acknowledge receipt of a broadcast
						if(status & 0x0010)
							status |= MilStd1553Symbol::STATUS_BROADCAST_ACK;

						//13 = busy
						if(status & 0x0008)
						{
							status |= MilStd1553Symbol::STATUS_BUSY;
							sstat += "Busy ";
						}

						//14 = subsystem fault
						if(status & 0x0004)
						{
							status |= MilStd1553Symbol::STATUS_SUBSYS_FAULT;
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
							sstat += "SubsysFault ";
						}

						//15 = dynamic bus accept
						if(status & 0x0002)
							status |= MilStd1553Symbol::STATUS_DYN_ACCEPT;

						//16 = terminal
						if(status & 0x0001)
						{
							status |= MilStd1553Symbol::STATUS_RT_FAULT;
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
							sstat += "RtFault ";
						}

						cap->m_offsets.push_back(bitstarts[7]);
						cap->m_durations.push_back(bitstarts[16] - bitstarts[7]);
						cap->m_samples.push_back(MilStd1553Symbol(MilStd1553Symbol::TYPE_STATUS, status ));

						pack->m_headers["Status"] = sstat;
					}

					//Parity bit
					cap->m_offsets.push_back(bitstarts[16]);
					cap->m_durations.push_back(timestamp - bitstarts[16]);
					if(expected_parity == parity)
						cap->m_samples.push_back(MilStd1553Symbol(MilStd1553Symbol::TYPE_PARITY_OK, parity));
					else
					{
						cap->m_samples.push_back(MilStd1553Symbol(MilStd1553Symbol::TYPE_PARITY_BAD, parity));
						pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
					}

					//If this is a RT->BC frame, now expect data
					if(ctrl_direction)
						frame_state = FRAME_STATE_DATA;

					//BC->RT, status was the last thing sent so now we're done
					else
					{
						pack->m_len = bitstarts[16] * din->m_timescale - pack->m_offset;
						frame_state = FRAME_STATE_IDLE;
					}

					state = STATE_IDLE;

					break;	//end FRAME_STATE_STATUS

				////////////////////////////////////////////////////////////////////////////////////////////////////////
				// Process a data word

				case FRAME_STATE_DATA:
					data_word_count ++;

					//Add the data sample
					cap->m_offsets.push_back(bitstarts[0]);
					cap->m_durations.push_back(bitstarts[16] - bitstarts[0]);
					cap->m_samples.push_back(MilStd1553Symbol(MilStd1553Symbol::TYPE_DATA, word));

					//Parity bit
					cap->m_offsets.push_back(bitstarts[16]);
					cap->m_durations.push_back(timestamp - bitstarts[16]);
					if(expected_parity == parity)
						cap->m_samples.push_back(MilStd1553Symbol(MilStd1553Symbol::TYPE_PARITY_OK, parity));
					else
					{
						cap->m_samples.push_back(MilStd1553Symbol(MilStd1553Symbol::TYPE_PARITY_BAD, parity));
						pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
					}

					//Save the data bytes
					pack->m_data.push_back(word >> 8);
					pack->m_data.push_back(word & 0xff);

					//Last word?
					if(data_word_count >= data_words_expected)
					{
						//If BC->RT, expect status
						if(!ctrl_direction)
						{
							frame_state = FRAME_STATE_STATUS;
							state = STATE_TURNAROUND;
						}

						//RT->BC, done after data
						else
						{
							frame_state = FRAME_STATE_IDLE;
							pack->m_len = bitstarts[16] * din->m_timescale - pack->m_offset;
							state = STATE_IDLE;
						}
					}

					//Expecting more data. No turnaround required.
					else
						state = STATE_IDLE;

					break;	//end FRAME_STATE_DATA
			}

			//Clear out word state
			bitstarts.clear();
			word = 0;
			bitcount = 0;
		}

		last_bit = current_bit;
	}

	cap->MarkModifiedFromCpu();
}

std::string MilStd1553Waveform::GetColor(size_t i)
{
	const MilStd1553Symbol& s = m_samples[i];
	switch(s.m_stype)
	{
		case MilStd1553Symbol::TYPE_SYNC_CTRL_STAT:
		case MilStd1553Symbol::TYPE_SYNC_DATA:
		case MilStd1553Symbol::TYPE_TURNAROUND:
			return StandardColors::colors[StandardColors::COLOR_PREAMBLE];

		case MilStd1553Symbol::TYPE_RT_ADDR:
		case MilStd1553Symbol::TYPE_SUB_ADDR:
			return StandardColors::colors[StandardColors::COLOR_ADDRESS];

		case MilStd1553Symbol::TYPE_DIRECTION:
		case MilStd1553Symbol::TYPE_LENGTH:
			return StandardColors::colors[StandardColors::COLOR_CONTROL];

		case MilStd1553Symbol::TYPE_DATA:
			return StandardColors::colors[StandardColors::COLOR_DATA];

		case MilStd1553Symbol::TYPE_PARITY_OK:
		case MilStd1553Symbol::TYPE_MSG_OK:
			return StandardColors::colors[StandardColors::COLOR_CHECKSUM_OK];

		case MilStd1553Symbol::TYPE_STATUS:
			if(s.m_data & MilStd1553Symbol::STATUS_ANY_FAULT)
				return StandardColors::colors[StandardColors::COLOR_ERROR];
			else
				return StandardColors::colors[StandardColors::COLOR_CONTROL];

		case MilStd1553Symbol::TYPE_PARITY_BAD:
		case MilStd1553Symbol::TYPE_MSG_ERR:
		case MilStd1553Symbol::TYPE_ERROR:
		default:
			return StandardColors::colors[StandardColors::COLOR_ERROR];
	}
}

string MilStd1553Waveform::GetText(size_t i)
{
	char tmp[128] = "";
	const MilStd1553Symbol& s = m_samples[i];
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
			return string("Len: ") + to_string(s.m_data);

		case MilStd1553Symbol::TYPE_PARITY_BAD:
		case MilStd1553Symbol::TYPE_PARITY_OK:
			return string("Parity: ") + to_string(s.m_data);

		case MilStd1553Symbol::TYPE_MSG_OK:
			return "Msg OK";
		case MilStd1553Symbol::TYPE_MSG_ERR:
			return "Msg error";

		case MilStd1553Symbol::TYPE_TURNAROUND:
			return "Turnaround";

		case MilStd1553Symbol::TYPE_STATUS:
			{
				string stmp;

				if(s.m_data & MilStd1553Symbol::STATUS_SERVICE_REQUEST)
					stmp += "ServiceReq ";
				if(s.m_data & MilStd1553Symbol::STATUS_MALFORMED)
					stmp += "(MALFORMED) ";
				if(s.m_data & MilStd1553Symbol::STATUS_BROADCAST_ACK)
					stmp += "BroadcastAck ";
				if(s.m_data & MilStd1553Symbol::STATUS_BUSY)
					stmp += "Busy ";
				if(s.m_data & MilStd1553Symbol::STATUS_SUBSYS_FAULT)
					stmp += "SubsystemFault ";
				if(s.m_data & MilStd1553Symbol::STATUS_DYN_ACCEPT)
					stmp += "DynAccept ";
				if(s.m_data & MilStd1553Symbol::STATUS_RT_FAULT)
					stmp += "RtFault ";

				if(stmp.empty())
					return "NoStatus";

				return stmp;
			}

		case MilStd1553Symbol::TYPE_DATA:
			snprintf(tmp, sizeof(tmp), "%04x", s.m_data);
			return tmp;

		case MilStd1553Symbol::TYPE_ERROR:
		default:
			return "ERROR";
	}
}
