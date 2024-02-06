/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2024 Andrew D. Zonenberg and contributors                                                         *
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
	@author Andrés MANELLI
	@brief Implementation of CANDecoder
 */

#include "../scopehal/scopehal.h"
#include "CANDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

CANDecoder::CANDecoder(const string& color)
	: PacketDecoder(color, CAT_BUS)
	, m_baudrateName("Bit Rate")
{
	CreateInput("CANH");

	m_parameters[m_baudrateName] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_BITRATE));
	m_parameters[m_baudrateName].SetIntVal(250000);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool CANDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	return false;
}

string CANDecoder::GetProtocolName()
{
	return "CAN";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void CANDecoder::Refresh()
{
	ClearPackets();

	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din = GetInputWaveform(0);
	din->PrepareForCpuAccess();
	auto udiff = dynamic_cast<UniformDigitalWaveform*>(din);
	auto sdiff = dynamic_cast<SparseDigitalWaveform*>(din);

	//Create the capture
	auto cap = new CANWaveform;
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	cap->m_triggerPhase = din->m_triggerPhase;
	cap->PrepareForCpuAccess();

	//Calculate some time scale values
	//Sample point is 3/4 of the way through the UI
	auto bitrate = m_parameters[m_baudrateName].GetIntVal();
	int64_t fs_per_ui = FS_PER_SECOND / bitrate;
	int64_t samples_per_ui = fs_per_ui / din->m_timescale;

	enum
	{
		STATE_WAIT_FOR_IDLE,
		STATE_IDLE,
		STATE_SOF,
		STATE_ID,
		STATE_EXT_ID,
		STATE_RTR,
		STATE_IDE,
		STATE_FD,
		STATE_R0,
		STATE_DLC,
		STATE_DATA,
		STATE_CRC,

		STATE_CRC_DELIM,
		STATE_ACK,
		STATE_ACK_DELIM,
		STATE_EOF
	} state = STATE_WAIT_FOR_IDLE;

	//LogDebug("Starting CAN decode\n");
	//LogIndenter li;

	Packet* pack = NULL;

	size_t len = din->size();
	int64_t tbitstart = 0;
	int64_t tblockstart = 0;
	bool vlast = true;
	int nbit = 0;
	bool sampled = false;
	bool sampled_value = false;
	bool last_sampled_value = false;
	int bits_since_toggle = 0;
	uint32_t current_field = 0;
	bool frame_is_rtr = false;
	bool extended_id = false;
	bool fd_mode = false;
	int frame_bytes_left = 0;
	int32_t frame_id = 0;
	char tmp[128];

	// CRC (http://esd.cs.ucr.edu/webres/can20.pdf page 13)
	const uint16_t crc_poly = 0x4599;
	uint16_t crc = 0;

	for(size_t i = 0; i < len; i++)
	{
		bool v = GetValue(sdiff, udiff, i);
		bool toggle = (v != vlast);
		vlast = v;

		auto off = ::GetOffset(sdiff, udiff, i);
		auto end = ::GetDuration(sdiff, udiff, i) + off;

		auto current_bitlen = off - tbitstart;

		//When starting up, wait until we have at least 7 UIs idle in a row
		if(state == STATE_WAIT_FOR_IDLE)
		{
			if(v)
				tblockstart = off;
			else
			{
				if( (off - tblockstart) >= (7 * samples_per_ui) )
					state = STATE_IDLE;
			}
		}

		//If we're idle, begin the SOF as soon as we hit a dominant state
		if(state == STATE_IDLE)
		{
			if(v)
			{
				tblockstart = off;
				tbitstart = off;
				nbit = 0;
				bits_since_toggle = 0;
				state = STATE_SOF;
			}
			continue;
		}

		//Ignore all transitions for the first half of the unit interval
		//TODO: resync if we get one in the very early period
		if(current_bitlen < samples_per_ui/2)
			continue;

		//When we hit 3/4 of a UI, sample the bit value.
		//Invert the sampled value since CAN uses negative logic
		if( (current_bitlen >= 3 * samples_per_ui / 4) && !sampled )
		{
			last_sampled_value = sampled_value;
			sampled = true;
			sampled_value = !v;
		}

		//Lock in a bit when either the UI ends, or we see a transition
		if(toggle || (current_bitlen >= samples_per_ui) )
		{
			/*
			LogDebug("Bit ended at %s (bits_since_toggle = %d, sampled_value = %d, last_sampled_value = %d)\n",
				Unit(Unit::UNIT_FS).PrettyPrint(off * diff->m_timescale).c_str(), bits_since_toggle,
				sampled_value, last_sampled_value);
			*/

			if(sampled_value == last_sampled_value)
				bits_since_toggle ++;

			//Don't look for stuff bits at the end of the frame
			else if(state >= STATE_ACK)
			{}

			//Discard stuff bits
			else
			{
				if(bits_since_toggle == 5)
				{
					//LogDebug("Discarding stuff bit at %s (bits_since_toggle = %d)\n",
					//	Unit(Unit::UNIT_FS).PrettyPrint(off * diff->m_timescale).c_str(), bits_since_toggle);

					tbitstart = off;
					sampled = false;
					bits_since_toggle = 1;
					continue;
				}
				else
					bits_since_toggle = 1;
			}

			//TODO: Detect and report an error if we see six consecutive bits with the same polarity

			//Read data bits
			current_field <<= 1;
			if(sampled_value)
				current_field |= 1;
			nbit ++;

			if (state != STATE_CRC){
				uint16_t crc_bit_14 = (crc >> 14) & 0x1;
				uint16_t crc_nxt = sampled_value ^ crc_bit_14;
				crc = crc << 1;

				if (crc_nxt){
					crc = crc ^ crc_poly;
				}
			}

			switch(state)
			{
				//Wait for at least 7 bit times low
				case STATE_WAIT_FOR_IDLE:
					break;

				case STATE_IDLE:
					break;

				//SOF bit is over
				case STATE_SOF:

					//Start a new packet
					pack = new Packet;
					pack->m_offset = off * din->m_timescale;
					pack->m_len = 0;
					m_packets.push_back(pack);

					cap->m_offsets.push_back(tblockstart);
					cap->m_durations.push_back(off - tblockstart);
					cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_SOF, 0));

					extended_id = false;
					fd_mode = false;

					tblockstart = off;
					nbit = 0;
					crc = 0;
					current_field = 0;
					state = STATE_ID;
					break;

				//Read the ID (MSB first)
				case STATE_ID:

					//When we've read 11 bits, the ID is over
					if(nbit == 11)
					{
						cap->m_offsets.push_back(tblockstart);
						cap->m_durations.push_back(end - tblockstart);
						cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_ID, current_field));

						state = STATE_RTR;

						frame_id = current_field;

						snprintf(tmp, sizeof(tmp), "%03x", frame_id);
						pack->m_headers["ID"] = tmp;
						pack->m_headers["Format"] = "Base";
						pack->m_headers["Mode"] = "CAN";
						pack->m_headers["Type"] = "Data";
					}

					break;

				//Remote transmission request
				case STATE_RTR:
					frame_is_rtr = sampled_value;

					cap->m_offsets.push_back(tbitstart);
					cap->m_durations.push_back(end - tbitstart);
					cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_RTR, frame_is_rtr));

					if(frame_is_rtr)
					{
						pack->m_headers["Type"] = "RTR";
						pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
					}
					else
						pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];

					if(extended_id)
						state = STATE_FD;
					else
						state = STATE_IDE;

					break;

				//Identifier extension
				case STATE_IDE:
					extended_id = sampled_value;

					if(extended_id)
					{
						//Delete the old ID and SRR
						for(int n=0; n<2; n++)
						{
							cap->m_offsets.pop_back();
							cap->m_durations.pop_back();
							cap->m_samples.pop_back();
						}

						nbit = 0;
						current_field = 0;
						state = STATE_EXT_ID;
					}

					else
						state = STATE_R0;

					break;

				//Full ID
				case STATE_EXT_ID:

					//Read the other 18 bits of the ID
					if(nbit == 18)
					{
						frame_id = (frame_id << 18) | current_field;

						cap->m_offsets.push_back(tblockstart);
						cap->m_durations.push_back(end - tblockstart);
						cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_ID, frame_id));

						snprintf(tmp, sizeof(tmp), "%08x", frame_id);
						pack->m_headers["ID"] = tmp;
						pack->m_headers["Format"] = "Ext";

						state = STATE_RTR;
					}

					break;

				//Reserved bit (should always be zero)
				case STATE_R0:
					cap->m_offsets.push_back(tbitstart);
					cap->m_durations.push_back(end - tbitstart);
					cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_R0, sampled_value));

					state = STATE_DLC;
					tblockstart = off;
					nbit = 0;
					current_field = 0;
					break;

				//FD mode (currently ignored)
				case STATE_FD:
					cap->m_offsets.push_back(tbitstart);
					cap->m_durations.push_back(end - tbitstart);
					cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_FD, sampled_value));

					fd_mode = sampled_value;
					if(fd_mode)
						pack->m_headers["Mode"] = "CAN-FD";

					state = STATE_R0;
					break;

				//Data length code (4 bits)
				case STATE_DLC:

					//When we've read 4 bits, the DLC is over
					if(nbit == 4)
					{
						cap->m_offsets.push_back(tblockstart);
						cap->m_durations.push_back(end - tblockstart);
						cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_DLC, current_field));

						frame_bytes_left = current_field;

						//Skip data if DLC=0, or if this is a read request
						if( (frame_bytes_left == 0) || frame_is_rtr)
							state = STATE_CRC;
						else
							state = STATE_DATA;

						tblockstart = end;
						nbit = 0;
						current_field = 0;
					}

					break;

				//Read frame data
				case STATE_DATA:

					//Data is in 8-bit bytes
					if(nbit == 8)
					{
						cap->m_offsets.push_back(tblockstart);
						cap->m_durations.push_back(end - tblockstart);
						cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_DATA, current_field));

						pack->m_data.push_back(current_field);

						//Go to CRC after we've read all the data
						frame_bytes_left --;
						if(frame_bytes_left == 0)
							state = STATE_CRC;

						//Reset for the next byte
						tblockstart = end;
						nbit = 0;
						current_field = 0;
					}

					break;

				//Read CRC value
				case STATE_CRC:

					//CRC is 15 bits long
					if(nbit == 15)
					{
						bool crc_ok = (current_field == (crc & 0x7fff));
						auto type = crc_ok ? CANSymbol::TYPE_CRC_OK : CANSymbol::TYPE_CRC_BAD;

						cap->m_offsets.push_back(tblockstart);
						cap->m_durations.push_back(end - tblockstart);
						cap->m_samples.push_back(CANSymbol(type, current_field));

						state = STATE_CRC_DELIM;
					}

					break;

				//CRC delimiter
				case STATE_CRC_DELIM:
					cap->m_offsets.push_back(tbitstart);
					cap->m_durations.push_back(end - tbitstart);
					cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_CRC_DELIM, sampled_value));

					state = STATE_ACK;
					break;

				//ACK bit
				case STATE_ACK:
					cap->m_offsets.push_back(tbitstart);
					cap->m_durations.push_back(end - tbitstart);
					cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_ACK, sampled_value));

					if(sampled_value)
						pack->m_headers["Ack"] = "NAK";
					else
						pack->m_headers["Ack"] = "ACK";

					state = STATE_ACK_DELIM;
					break;

				//ACK delimiter
				case STATE_ACK_DELIM:
					cap->m_offsets.push_back(tbitstart);
					cap->m_durations.push_back(end - tbitstart);
					cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_ACK_DELIM, sampled_value));

					state = STATE_EOF;
					tblockstart = end;
					nbit = 0;
					current_field = 0;
					break;

				//Read EOF
				case STATE_EOF:

					//EOF is 7 bits long
					if(nbit == 7)
					{
						if(frame_is_rtr)
							snprintf(tmp, sizeof(tmp), "%d", (int)frame_bytes_left);
						else
							snprintf(tmp, sizeof(tmp), "%d", (int)pack->m_data.size());
						pack->m_headers["Len"] = tmp;

						cap->m_offsets.push_back(tblockstart);
						cap->m_durations.push_back(end - tblockstart);
						cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_EOF, current_field));

						pack->m_len = pack->m_offset - (end * din->m_timescale);

						state = STATE_IDLE;
					}

					break;

				default:
					break;
			}

			//Start the next bit
			tbitstart = off;
			sampled = false;
		}
	}

	SetData(cap, 0);

	cap->MarkModifiedFromCpu();
}

vector<string> CANDecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("ID");
	ret.push_back("Mode");
	ret.push_back("Format");
	ret.push_back("Type");
	ret.push_back("Ack");
	ret.push_back("Len");
	return ret;
}
