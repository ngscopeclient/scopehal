/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2021 Andrew D. Zonenberg                                                                          *
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
#include "ESPIDecoder.h"
#include "SPIDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ESPIDecoder::ESPIDecoder(const string& color)
	: PacketDecoder(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_BUS)
{
	CreateInput("clk");
	CreateInput("cs#");
	CreateInput("dq3");
	CreateInput("dq2");
	CreateInput("dq1");
	CreateInput("dq0");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool ESPIDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if(
		(i < 6) &&
		(stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) &&
		(stream.m_channel->GetWidth() == 1)
		)
	{
		return true;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string ESPIDecoder::GetProtocolName()
{
	return "Intel eSPI";
}

bool ESPIDecoder::IsOverlay()
{
	return true;
}

bool ESPIDecoder::NeedsConfig()
{
	return true;
}

void ESPIDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "eSPI(%s, %s, %s, %s)",
		GetInputDisplayName(2).c_str(),
		GetInputDisplayName(3).c_str(),
		GetInputDisplayName(4).c_str(),
		GetInputDisplayName(5).c_str()
		);
	m_hwname = hwname;
	m_displayname = m_hwname;
}

vector<string> ESPIDecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("Command");
	ret.push_back("Address");
	ret.push_back("Response");
	ret.push_back("Status");
	return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void ESPIDecoder::Refresh()
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
	auto csn = GetDigitalInputWaveform(1);
	auto data3 = GetDigitalInputWaveform(2);
	auto data2 = GetDigitalInputWaveform(3);
	auto data1 = GetDigitalInputWaveform(4);
	auto data0 = GetDigitalInputWaveform(5);

	size_t clklen = clk->m_samples.size();
	size_t cslen = csn->m_samples.size();
	size_t datalen[4] =
	{
		data0->m_samples.size(),
		data1->m_samples.size(),
		data2->m_samples.size(),
		data3->m_samples.size()
	};

	size_t ics			= 0;
	size_t iclk			= 0;
	size_t idata[4]		= {0};
	int64_t timestamp	= 0;

	//Create the waveform. Call SetData() early on so we can use GetText() in the packet decode
	auto cap = new ESPIWaveform;
	cap->m_timescale = clk->m_timescale;
	cap->m_startTimestamp = clk->m_startTimestamp;
	cap->m_startFemtoseconds = clk->m_startFemtoseconds;
	SetData(cap, 0);

	ESPISymbol samp;
	enum
	{
		LINK_STATE_DESELECTED,
		LINK_STATE_SELECTED_CLKLO,
		LINK_STATE_SELECTED_CLKHI

	} link_state = LINK_STATE_DESELECTED;

	enum
	{
		TXN_STATE_IDLE,

		TXN_STATE_OPCODE,
		TXN_STATE_CONFIG_ADDRESS,
		TXN_STATE_CONFIG_DATA,
		TXN_STATE_COMMAND_CRC8,

		TXN_STATE_RESPONSE,
		TXN_STATE_RESPONSE_DATA,
		TXN_STATE_STATUS,
		TXN_STATE_RESPONSE_CRC8

	} txn_state = TXN_STATE_IDLE;

	ESPISymbol::ESpiCommand current_cmd = ESPISymbol::COMMAND_RESET;
	Packet* pack = NULL;

	enum
	{
		READ_SI,
		READ_SO,
		READ_QUAD
	} read_mode = READ_SI;

	size_t count = 0;
	size_t tstart = 0;
	uint8_t crc = 0;
	uint64_t data = 0;

	int skip_bits			= 0;
	int bitcount			= 0;
	int64_t bytestart		= 0;
	uint8_t current_byte	= 0;
	bool byte_valid_next	= false;
	ESPISymbol::ESpiCompletion completion_type	= ESPISymbol::COMPLETION_NONE;

	while(true)
	{
		bool cur_cs = csn->m_samples[ics];
		bool cur_clk = clk->m_samples[iclk];
		uint8_t cur_data =
			(data3->m_samples[idata[3]] ? 0x8 : 0) |
			(data2->m_samples[idata[2]] ? 0x4 : 0) |
			(data1->m_samples[idata[1]] ? 0x2 : 0) |
			(data0->m_samples[idata[0]] ? 0x1 : 0);

		bool byte_valid = false;

		switch(link_state)
		{
			case LINK_STATE_DESELECTED:
				if(!cur_cs)
				{
					link_state = LINK_STATE_SELECTED_CLKLO;
					current_byte = 0;
					bitcount = 0;
					bytestart = timestamp;

					//Start a new packet
					txn_state = TXN_STATE_OPCODE;
					crc = 0;
				}
				break;	//end LINK_STATE_DESELECTED

			//wait for rising edge of clk
			case LINK_STATE_SELECTED_CLKLO:
				if(cur_clk)
				{
					if(skip_bits > 0)
					{
						skip_bits --;
						bytestart = timestamp;
					}
					else
					{
						//If this is the beginning of a byte, see if either DQ2 or DQ3 is low.
						//This means they're actively driven (since they have pullups) and means
						//that we're definitely in quad mode.
						if(bitcount == 0)
						{
							if( (cur_data & 0xc) != 0xc)
								read_mode = READ_QUAD;
						}

						switch(read_mode)
						{
							case READ_SI:
								bitcount ++;
								current_byte <<= 1;
								current_byte |= (cur_data & 1);
								break;

							case READ_SO:
								bitcount ++;
								current_byte <<= 1;
								current_byte |= (cur_data & 2) >> 1;
								break;

							case READ_QUAD:
								bitcount += 4;
								current_byte <<= 4;
								current_byte |= cur_data;
								break;
						}

						if(bitcount == 8)
						{
							byte_valid_next = true;
							bitcount = 0;
						}
					}

					link_state = LINK_STATE_SELECTED_CLKHI;
				}

				break;	//end LINK_STATE_SELECTED_CLKLO

			//wait for falling edge of clk
			case LINK_STATE_SELECTED_CLKHI:
				if(!cur_clk)
				{
					link_state = LINK_STATE_SELECTED_CLKLO;
					if(byte_valid_next)
					{
						byte_valid = true;
						byte_valid_next = false;
					}
				}
				break;
		}

		//end of packet
		//TODO: error if a byte is truncated
		if( (link_state != LINK_STATE_DESELECTED) && cur_cs)
		{
			if(pack)
			{
				pack->m_len = (timestamp * clk->m_timescale) - pack->m_offset;
				pack = NULL;
			}

			bytestart = timestamp;
			link_state = LINK_STATE_DESELECTED;
			read_mode = READ_SI;
		}

		if(byte_valid)
		{
			switch(txn_state)
			{
				//Nothign to do
				case TXN_STATE_IDLE:
					break;	//end STATE_IDLE

				//Frame should begin with an opcode
				case TXN_STATE_OPCODE:

					//Create a new packet
					pack = new Packet;
					pack->m_len = 0;
					m_packets.push_back(pack);

					current_cmd = (ESPISymbol::ESpiCommand)current_byte;

					//Add symbol for packet type
					tstart = timestamp;
					pack->m_offset = bytestart * clk->m_timescale;
					cap->m_offsets.push_back(bytestart);
					cap->m_durations.push_back(timestamp - bytestart);
					cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_COMMAND_TYPE, current_byte));
					pack->m_headers["Command"] = GetText(cap->m_samples.size()-1);

					//Decide what to do based on the opcode
					count = 0;
					data = 0;

					switch(current_cmd)
					{
						//Expect a 16 bit address
						case ESPISymbol::COMMAND_GET_CONFIGURATION:
						case ESPISymbol::COMMAND_SET_CONFIGURATION:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_CONTROL];
							txn_state = TXN_STATE_CONFIG_ADDRESS;
							break;

						//TODO
						case ESPISymbol::COMMAND_PUT_IORD_SHORT_x1:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							txn_state = TXN_STATE_IDLE;
							break;
						case ESPISymbol::COMMAND_PUT_IORD_SHORT_x2:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							txn_state = TXN_STATE_IDLE;
							break;
						case ESPISymbol::COMMAND_PUT_IORD_SHORT_x4:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							txn_state = TXN_STATE_IDLE;
							break;

						//TODO
						case ESPISymbol::COMMAND_PUT_IOWR_SHORT_x1:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
							txn_state = TXN_STATE_IDLE;
							break;
						case ESPISymbol::COMMAND_PUT_IOWR_SHORT_x2:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
							txn_state = TXN_STATE_IDLE;
							break;
						case ESPISymbol::COMMAND_PUT_IOWR_SHORT_x4:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
							txn_state = TXN_STATE_IDLE;
							break;

						//TODO
						case ESPISymbol::COMMAND_PUT_PC:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
							txn_state = TXN_STATE_IDLE;
							break;
						case ESPISymbol::COMMAND_GET_PC:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							txn_state = TXN_STATE_IDLE;
							break;

						//TODO
						case ESPISymbol::COMMAND_GET_VWIRE:
						case ESPISymbol::COMMAND_PUT_VWIRE:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_CONTROL];
							txn_state = TXN_STATE_IDLE;
							break;

						//TODO
						case ESPISymbol::COMMAND_GET_STATUS:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_STATUS];
							txn_state = TXN_STATE_IDLE;
							break;

						//TODO
						case ESPISymbol::COMMAND_RESET:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_COMMAND];
							txn_state = TXN_STATE_IDLE;
							break;

						case ESPISymbol::COMMAND_GET_FLASH_NP:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							txn_state = TXN_STATE_IDLE;
							break;
						case ESPISymbol::COMMAND_PUT_FLASH_C:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
							txn_state = TXN_STATE_IDLE;
							break;

						case ESPISymbol::COMMAND_GET_OOB:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							txn_state = TXN_STATE_IDLE;
							break;
						case ESPISymbol::COMMAND_PUT_OOB:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
							txn_state = TXN_STATE_IDLE;
							break;

						//Unknown
						default:
							txn_state = TXN_STATE_IDLE;
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
							break;
					}

					break;	//end TXN_STATE_OPCODE

				case TXN_STATE_CONFIG_ADDRESS:

					//Save start time
					if(count == 0)
					{
						tstart = bytestart;
						cap->m_offsets.push_back(tstart);
					}

					//Save data
					data = (data << 8) | current_byte;
					count ++;

					//Add data
					if(count == 2)
					{
						cap->m_durations.push_back(timestamp - tstart);
						cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_CAPS_ADDR, data));
						pack->m_headers["Address"] = GetText(cap->m_samples.size()-1);

						if(current_cmd == ESPISymbol::COMMAND_SET_CONFIGURATION)
						{
							txn_state = TXN_STATE_CONFIG_DATA;
							data = 0;
							count = 0;
						}
						else
							txn_state = TXN_STATE_COMMAND_CRC8;
					}

					break;	//end TXN_STATE_CONFIG_ADDRESS

				case TXN_STATE_CONFIG_DATA:

					//Save start time
					if(count == 0)
					{
						tstart = bytestart;
						cap->m_offsets.push_back(tstart);
					}

					//Save data
					data = (data << 8) | current_byte;
					pack->m_data.push_back(current_byte);
					count ++;

					//Add data
					if(count == 4)
					{
						cap->m_durations.push_back(timestamp - tstart);
						cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_COMMAND_DATA_32, data));

						txn_state = TXN_STATE_COMMAND_CRC8;
					}

					break;	//end TXN_STATE_CONFIG_DATA

				case TXN_STATE_COMMAND_CRC8:

					cap->m_offsets.push_back(bytestart);
					cap->m_durations.push_back(timestamp - bytestart);
					if(current_byte == crc)
						cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_COMMAND_CRC_GOOD, current_byte));
					else
					{
						cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_COMMAND_CRC_BAD, current_byte));
						pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
					}

					switch(current_cmd)
					{
						//Expect a response after a 2-cycle bus turnaround
						case ESPISymbol::COMMAND_GET_CONFIGURATION:
							txn_state = TXN_STATE_RESPONSE;
							skip_bits = 2;
							break;

						//don't know what to do
						default:
							txn_state = TXN_STATE_IDLE;
							break;
					}

					//If running in x1 mode, switch to reading the other data line.
					//If in x4 mode, no action needed.
					if(read_mode == READ_SI)
						read_mode = READ_SO;

					break;	//end TXN_STATE_COMMAND_CRC8

				case TXN_STATE_RESPONSE:

					//Start with a fresh CRC for the response phase
					crc = 0;

					cap->m_offsets.push_back(bytestart);
					cap->m_durations.push_back(timestamp - bytestart);
					cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_RESPONSE_OP, current_byte));

					completion_type = static_cast<ESPISymbol::ESpiCompletion>(current_byte >> 6);

					pack->m_headers["Response"] = GetText(cap->m_samples.size()-1);

					switch(current_cmd)
					{
						case ESPISymbol::COMMAND_GET_CONFIGURATION:
							txn_state = TXN_STATE_RESPONSE_DATA;
							count = 0;
							data = 0;
							break;

						default:
							txn_state = TXN_STATE_IDLE;
					}

					break;	//end TXN_STATE_RESPONSE

				case TXN_STATE_RESPONSE_DATA:

					if(count == 0)
					{
						tstart = bytestart;
						cap->m_offsets.push_back(tstart);
					}

					//per page 93, data is LSB to MSB
					data |= current_byte << ( (count & 3) * 8);
					count ++;

					pack->m_data.push_back(current_byte);

					//TODO: different commands have different lengths for reply data
					if(count == 4)
					{
						cap->m_durations.push_back(timestamp - tstart);
						cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_RESPONSE_DATA_32, data));

						count = 0;
						data = 0;
						txn_state = TXN_STATE_STATUS;
					}

					break;	//end TXN_STATE_RESPONSE_DATA

				case TXN_STATE_STATUS:

					//Save start time
					if(count == 0)
					{
						tstart = bytestart;
						cap->m_offsets.push_back(tstart);
					}

					//Save data (LSB to MSB)
					data |= current_byte << ( (count & 3) * 8);
					count ++;

					//Add data
					if(count == 2)
					{
						cap->m_durations.push_back(timestamp - tstart);
						cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_RESPONSE_STATUS, data));

						txn_state = TXN_STATE_RESPONSE_CRC8;
					}

					break;	//end TXN_STATE_STATUS

				case TXN_STATE_RESPONSE_CRC8:

					cap->m_offsets.push_back(bytestart);
					cap->m_durations.push_back(timestamp - bytestart);
					if(current_byte == crc)
						cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_RESPONSE_CRC_GOOD, current_byte));
					else
					{
						cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_RESPONSE_CRC_BAD, current_byte));
						pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
					}

					//Done with the packet
					txn_state = TXN_STATE_IDLE;
					break;	//end TXN_STATE_COMMAND_CRC8
			}

			//Checksum this byte
			crc = UpdateCRC8(crc, current_byte);

			bytestart = timestamp;
		}

		//Get timestamps of next event on each channel
		int64_t next_cs = GetNextEventTimestamp(csn, ics, cslen, timestamp);
		int64_t next_clk = GetNextEventTimestamp(clk, iclk, clklen, timestamp);

		//If we can't move forward, stop (don't bother looking for glitches on data)
		int64_t next_timestamp = min(next_clk, next_cs);
		if(next_timestamp == timestamp)
			break;

		//All good, move on
		timestamp = next_timestamp;
		AdvanceToTimestamp(csn, ics, cslen, timestamp);
		AdvanceToTimestamp(clk, iclk, clklen, timestamp);
		AdvanceToTimestamp(data0, idata[0], datalen[0], timestamp);
		AdvanceToTimestamp(data1, idata[1], datalen[1], timestamp);
		AdvanceToTimestamp(data2, idata[2], datalen[2], timestamp);
		AdvanceToTimestamp(data3, idata[3], datalen[3], timestamp);
	}
}

uint8_t ESPIDecoder::UpdateCRC8(uint8_t crc, uint8_t data)
{
	//CRC runs MSB first using polynomial x^8 + x^2 + x + 1
	for(int i=7; i>=0; i--)
	{
		//Shift CRC left and save the high bit
		uint8_t hi = crc >> 7;
		crc = (crc << 1);

		//Mix in the new data bit
		hi ^= ( (data >> i) & 1);
		if(hi)
			crc ^= 0x7;
	}

	return crc;
}

Gdk::Color ESPIDecoder::GetColor(int i)
{
	auto capture = dynamic_cast<ESPIWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const ESPISymbol& s = capture->m_samples[i];

		switch(s.m_type)
		{
			case ESPISymbol::TYPE_COMMAND_TYPE:
			case ESPISymbol::TYPE_RESPONSE_OP:
			case ESPISymbol::TYPE_RESPONSE_STATUS:
				return m_standardColors[COLOR_CONTROL];

			case ESPISymbol::TYPE_CAPS_ADDR:
			/*case ESPISymbol::TYPE_COMMAND_ADDR_16:
			case ESPISymbol::TYPE_COMMAND_ADDR_32:
			case ESPISymbol::TYPE_COMMAND_ADDR_64:*/
				return m_standardColors[COLOR_ADDRESS];

			case ESPISymbol::TYPE_COMMAND_CRC_GOOD:
			case ESPISymbol::TYPE_RESPONSE_CRC_GOOD:
				return m_standardColors[COLOR_CHECKSUM_OK];
			case ESPISymbol::TYPE_COMMAND_CRC_BAD:
			case ESPISymbol::TYPE_RESPONSE_CRC_BAD:
				return m_standardColors[COLOR_CHECKSUM_BAD];

			case ESPISymbol::TYPE_COMMAND_DATA_32:
			case ESPISymbol::TYPE_RESPONSE_DATA_32:
				return m_standardColors[COLOR_DATA];

			default:
				return m_standardColors[COLOR_ERROR];
		}
	}

	return m_standardColors[COLOR_ERROR];
}

string ESPIDecoder::GetText(int i)
{
	auto capture = dynamic_cast<ESPIWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const ESPISymbol& s = capture->m_samples[i];
		char tmp[128];
		string stmp;

		switch(s.m_type)
		{
			case ESPISymbol::TYPE_COMMAND_TYPE:
				switch(s.m_data)
				{
					case ESPISymbol::COMMAND_GET_CONFIGURATION:
						return "Get Configuration";
					case ESPISymbol::COMMAND_SET_CONFIGURATION:
						return "Set Configuration";

					case ESPISymbol::COMMAND_GET_OOB:
						return "Get OOB";
					case ESPISymbol::COMMAND_PUT_OOB:
						return "Put OOB";

					case ESPISymbol::COMMAND_GET_PC:
						return "Get PC";
					case ESPISymbol::COMMAND_PUT_PC:
						return "Put PC";

					case ESPISymbol::COMMAND_GET_STATUS:
						return "Get Status";

					case ESPISymbol::COMMAND_GET_FLASH_NP:
						return "Get Flash Non-Posted";
					case ESPISymbol::COMMAND_PUT_FLASH_C:
						return "Put Flash Completion";

					case ESPISymbol::COMMAND_GET_VWIRE:
						return "Get Virtual Wire";
					case ESPISymbol::COMMAND_PUT_VWIRE:
						return "Put Virtual Wire";

					case ESPISymbol::COMMAND_PUT_IOWR_SHORT_x1:
					case ESPISymbol::COMMAND_PUT_IOWR_SHORT_x2:
					case ESPISymbol::COMMAND_PUT_IOWR_SHORT_x4:
						return "Put I/O Write";

					case ESPISymbol::COMMAND_PUT_IORD_SHORT_x1:
					case ESPISymbol::COMMAND_PUT_IORD_SHORT_x2:
					case ESPISymbol::COMMAND_PUT_IORD_SHORT_x4:
						return "Put I/O Read";

					default:
						snprintf(tmp, sizeof(tmp), "Unknown Cmd (%02lx)", s.m_data);
						return tmp;
				}
				break;

			case ESPISymbol::TYPE_CAPS_ADDR:
				switch(s.m_data)
				{
					case 0x04:	return "Device ID";
					case 0x08:	return "General Capabilities";
					case 0x10:	return "CH0 Capabilities";
					case 0x20:	return "CH1 Capabilities";
					case 0x30:	return "CH2 Capabilities";
					case 0x40:	return "CH3 Capabilities";

					//Print as hex if unknown
					default:
						snprintf(tmp, sizeof(tmp), "%04lx", s.m_data);
						return tmp;
				}

			case ESPISymbol::TYPE_COMMAND_CRC_GOOD:
			case ESPISymbol::TYPE_COMMAND_CRC_BAD:
			case ESPISymbol::TYPE_RESPONSE_CRC_GOOD:
			case ESPISymbol::TYPE_RESPONSE_CRC_BAD:
				snprintf(tmp, sizeof(tmp), "CRC: %02lx", s.m_data);
				return tmp;

			case ESPISymbol::TYPE_RESPONSE_OP:
				switch(s.m_data & 0xf)
				{
					case ESPISymbol::RESPONSE_DEFER:
						return "Defer";

					case ESPISymbol::RESPONSE_NONFATAL_ERROR:
						return "Nonfatal Error";

					case ESPISymbol::RESPONSE_FATAL_ERROR:
						return "Fatal Error";

					case ESPISymbol::RESPONSE_ACCEPT:
						return "Accept";

					case ESPISymbol::RESPONSE_NONE:
						return "No Response";

					default:
						snprintf(tmp, sizeof(tmp), "Unknown response %lx", s.m_data & 0xf);
						return tmp;
				}
				break;

			case ESPISymbol::TYPE_COMMAND_DATA_32:
				snprintf(tmp, sizeof(tmp), "%08lx", s.m_data);
				return tmp;

			case ESPISymbol::TYPE_RESPONSE_DATA_32:
				snprintf(tmp, sizeof(tmp), "%08lx", s.m_data);
				return tmp;

			case ESPISymbol::TYPE_RESPONSE_STATUS:
				if(s.m_data & 0x2000)
					stmp += "FLASH_NP_AVAIL ";
				if(s.m_data & 0x1000)
					stmp += "FLASH_C_AVAIL ";
				if(s.m_data & 0x0200)
					stmp += "FLASH_NP_FREE ";
				if(s.m_data & 0x0080)
					stmp += "OOB_AVAIL ";
				if(s.m_data & 0x0040)
					stmp += "VWIRE_AVAIL ";
				if(s.m_data & 0x0020)
					stmp += "NP_AVAIL ";
				if(s.m_data & 0x0010)
					stmp += "PC_AVAIL ";
				if(s.m_data & 0x0008)
					stmp += "OOB_FREE ";
				if(s.m_data & 0x0002)
					stmp += "NP_FREE ";
				if(s.m_data & 0x0001)
					stmp += "PC_FREE";
				return stmp;

			/*case ESPISymbol::TYPE_COMMAND_ADDR_16:
				snprintf(tmp, sizeof(tmp), "Addr: %04lx", s.m_data);
				return tmp;

			case ESPISymbol::TYPE_COMMAND_ADDR_32:
				snprintf(tmp, sizeof(tmp), "Addr: %08lx", s.m_data);
				return tmp;

			case ESPISymbol::TYPE_COMMAND_ADDR_64:
				snprintf(tmp, sizeof(tmp), "Addr: %016lx", s.m_data);
				return tmp;*/

			case ESPISymbol::TYPE_ERROR:
			default:
				return "ERROR";
		}
	}
	return "";
}

bool ESPIDecoder::CanMerge(Packet* first, Packet* /*cur*/, Packet* next)
{
	/*
	//Merge read-status packets
	if( (first->m_headers["Op"] == "Read Status") && (next->m_headers["Op"] == "Read Status") )
		return true;
	*/
	return false;
}

Packet* ESPIDecoder::CreateMergedHeader(Packet* pack, size_t /*i*/)
{
	/*
	if(pack->m_headers["Op"] == "Read Status")
	{
		Packet* ret = new Packet;
		ret->m_offset = pack->m_offset;
		ret->m_len = pack->m_len;			//TODO: extend?
		ret->m_headers["Op"] = "Poll Status";
		ret->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_STATUS];

		//TODO: add other fields?
		return ret;
	}*/
	return NULL;
}
