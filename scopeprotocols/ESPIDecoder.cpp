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
	ret.push_back("Len");
	ret.push_back("Tag");
	ret.push_back("Info");
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
		TXN_STATE_RESPONSE_CRC8,

		TXN_STATE_VWIRE_COUNT,
		TXN_STATE_VWIRE_INDEX,
		TXN_STATE_VWIRE_DATA,

		TXN_STATE_FLASH_TYPE,
		TXN_STATE_FLASH_TAG_LENHI,
		TXN_STATE_FLASH_LENLO,
		TXN_STATE_FLASH_ADDR

	} txn_state = TXN_STATE_IDLE;

	ESPISymbol::ESpiCommand current_cmd = ESPISymbol::COMMAND_RESET;
	Packet* pack = NULL;

	enum
	{
		READ_SI,
		READ_SO,
		READ_QUAD_RISING,
		READ_QUAD_FALLING
	} read_mode = READ_SI;

	size_t count = 0;
	size_t tstart = 0;
	uint8_t crc = 0;
	uint64_t data = 0;
	uint64_t addr = 0;

	int skip_bits			= 0;
	bool skip_next_falling	= false;
	int bitcount			= 0;
	int64_t bytestart		= 0;
	uint8_t current_byte	= 0;
	bool byte_valid_next	= false;
	ESPISymbol::ESpiCompletion completion_type	= ESPISymbol::COMPLETION_NONE;
	ESPISymbol::ESpiFlashType flash_type = ESPISymbol::FLASH_READ;

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
						skip_next_falling = true;
					}
					else
					{
						skip_next_falling = false;

						//If this is the beginning of a byte, see if either DQ2 or DQ3 is low.
						//This means they're actively driven (since they have pullups) and means
						//that we're definitely in quad mode.
						if(bitcount == 0)
						{
							switch(read_mode)
							{
								case READ_SI:
								case READ_SO:
									if( (cur_data & 0xc) != 0xc)
										read_mode = READ_QUAD_RISING;
									break;

								default:
									break;
							}
						}

						//Sample on rising edges
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

							case READ_QUAD_RISING:
								bitcount += 4;
								current_byte <<= 4;
								current_byte |= cur_data;
								break;

							//READ_QUAD_FALLING handled in LINK_STATE_SELECTED_CLKHI
							default:
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
					if( (read_mode == READ_QUAD_FALLING) && !skip_next_falling)
					{
						bitcount += 4;
						current_byte <<= 4;
						current_byte |= cur_data;

						if(bitcount == 8)
						{
							byte_valid_next = true;
							bitcount = 0;
						}
					}

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
				////////////////////////////////////////////////////////////////////////////////////////////////////////
				// Generic command parsing

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
					addr = 0;

					switch(current_cmd)
					{
						//Expect a 16 bit address
						case ESPISymbol::COMMAND_GET_CONFIGURATION:
						case ESPISymbol::COMMAND_SET_CONFIGURATION:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_CONTROL];
							txn_state = TXN_STATE_CONFIG_ADDRESS;
							break;

						//No arguments
						case ESPISymbol::COMMAND_GET_STATUS:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_STATUS];
							txn_state = TXN_STATE_COMMAND_CRC8;
							break;
						case ESPISymbol::COMMAND_GET_FLASH_NP:
						case ESPISymbol::COMMAND_GET_VWIRE:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							txn_state = TXN_STATE_COMMAND_CRC8;
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
						case ESPISymbol::COMMAND_PUT_VWIRE:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
							txn_state = TXN_STATE_IDLE;
							break;

						//TODO
						case ESPISymbol::COMMAND_RESET:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_COMMAND];
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
						case ESPISymbol::COMMAND_SET_CONFIGURATION:
						case ESPISymbol::COMMAND_GET_STATUS:
						case ESPISymbol::COMMAND_GET_FLASH_NP:
						case ESPISymbol::COMMAND_GET_VWIRE:
							txn_state = TXN_STATE_RESPONSE;
							skip_bits = 2;
							break;

						//don't know what to do
						default:
							txn_state = TXN_STATE_IDLE;
							break;
					}

					//Switch read polarity
					if(read_mode == READ_SI)
						read_mode = READ_SO;
					else if(read_mode == READ_QUAD_RISING)
						read_mode = READ_QUAD_FALLING;

					break;	//end TXN_STATE_COMMAND_CRC8

				////////////////////////////////////////////////////////////////////////////////////////////////////////
				// Configuration packets

				case TXN_STATE_CONFIG_ADDRESS:

					//Save start time
					if(count == 0)
					{
						tstart = bytestart;
						cap->m_offsets.push_back(tstart);
					}

					//Save data
					addr = (addr << 8) | current_byte;
					count ++;

					//Add data
					if(count == 2)
					{
						cap->m_durations.push_back(timestamp - tstart);
						cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_CAPS_ADDR, addr));
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
					data |= current_byte << ( (count & 3) * 8);
					pack->m_data.push_back(current_byte);
					count ++;

					//Add data
					if(count == 4)
					{
						cap->m_durations.push_back(timestamp - tstart);

						switch(current_cmd)
						{
							case ESPISymbol::COMMAND_SET_CONFIGURATION:
								switch(addr)
								{
									case 0x20:
										cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_CH1_CAPS_WR, data));
										pack->m_headers["Info"] = Trim(GetText(cap->m_samples.size()-1));
										break;

									case 0x30:
										cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_CH2_CAPS_WR, data));
										pack->m_headers["Info"] = Trim(GetText(cap->m_samples.size()-1));
										break;

									default:
										cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_COMMAND_DATA_32, data));
								}
								break;

							default:
								cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_COMMAND_DATA_32, data));
								break;
						}

						txn_state = TXN_STATE_COMMAND_CRC8;
					}

					break;	//end TXN_STATE_CONFIG_DATA

				////////////////////////////////////////////////////////////////////////////////////////////////////////
				// Generic reply packets

				case TXN_STATE_RESPONSE:

					//Start with a fresh CRC for the response phase
					crc = 0;

					cap->m_offsets.push_back(bytestart);
					cap->m_durations.push_back(timestamp - bytestart);
					cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_RESPONSE_OP, current_byte));

					completion_type = static_cast<ESPISymbol::ESpiCompletion>(current_byte >> 6);

					pack->m_headers["Response"] = GetText(cap->m_samples.size()-1);

					count = 0;
					data = 0;

					switch(current_cmd)
					{
						case ESPISymbol::COMMAND_GET_CONFIGURATION:
							txn_state = TXN_STATE_RESPONSE_DATA;
							break;

						case ESPISymbol::COMMAND_GET_STATUS:
							txn_state = TXN_STATE_STATUS;
							break;

						case ESPISymbol::COMMAND_GET_VWIRE:
							txn_state = TXN_STATE_VWIRE_COUNT;
							break;

						case ESPISymbol::COMMAND_GET_FLASH_NP:
							txn_state = TXN_STATE_FLASH_TYPE;
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

						switch(current_cmd)
						{
							case ESPISymbol::COMMAND_GET_CONFIGURATION:
								switch(addr)
								{
									case 0x8:
										cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_GENERAL_CAPS, data));
										pack->m_headers["Info"] = Trim(GetText(cap->m_samples.size()-1));
										break;

									case 0x20:
										cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_CH1_CAPS_RD, data));
										pack->m_headers["Info"] = Trim(GetText(cap->m_samples.size()-1));
										break;

									case 0x30:
										cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_CH2_CAPS_RD, data));
										pack->m_headers["Info"] = Trim(GetText(cap->m_samples.size()-1));
										break;

									default:
										cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_RESPONSE_DATA_32, data));
										break;
								}
								break;

							default:
								cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_RESPONSE_DATA_32, data));
								break;
						}

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

						//Don't report free space in the protocol analyzer
						//to save column space
						string tmp;
						if(data & 0x2000)
							tmp += "FLASH_NP_AVAIL ";
						if(data & 0x1000)
							tmp += "FLASH_C_AVAIL ";
						if(data & 0x0200)
							tmp += "FLASH_NP_FREE ";
						if(data & 0x0080)
							tmp += "OOB_AVAIL ";
						if(data & 0x0040)
							tmp += "VWIRE_AVAIL ";
						pack->m_headers["Status"] = tmp;

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
						LogDebug("Invalid response CRC (got %02x, expected %02x)\n", current_byte, crc);
						cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_RESPONSE_CRC_BAD, current_byte));
						pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
					}

					//Done with the packet
					txn_state = TXN_STATE_IDLE;
					break;	//end TXN_STATE_COMMAND_CRC8

				////////////////////////////////////////////////////////////////////////////////////////////////////////
				// Virtual wire channel

				case TXN_STATE_VWIRE_COUNT:
					cap->m_offsets.push_back(bytestart);
					cap->m_durations.push_back(timestamp - bytestart);
					cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_VWIRE_COUNT, current_byte));

					count = current_byte;

					txn_state = TXN_STATE_VWIRE_INDEX;
					break;	//end TXN_STATE_VWIRE_COUNT

				case TXN_STATE_VWIRE_INDEX:
					txn_state = TXN_STATE_VWIRE_DATA;

					addr = current_byte;

					cap->m_offsets.push_back(bytestart);
					cap->m_durations.push_back(timestamp - bytestart);
					cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_VWIRE_INDEX, current_byte));

					break;	//end TXN_STATE_VWIRE_INDEX

				case TXN_STATE_VWIRE_DATA:

					cap->m_offsets.push_back(bytestart);
					cap->m_durations.push_back(timestamp - bytestart);
					cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_VWIRE_DATA, current_byte));

					//Virtual wire indexes 0/1 are IRQs
					if(addr <= 1)
					{
						string tmp = "IRQ";
						if(addr == 0)
							tmp += to_string(current_byte & 0x7f);
						else
							tmp += to_string( (current_byte & 0x7f) + 128 );
						if(current_byte & 0x80)
							tmp += " high\n";
						else
							tmp += " low\n";

						pack->m_headers["Info"] += tmp;
					}

					//Indexes 2-7 are "system events".
					//See table 10-15
					else if(addr <= 7)
					{
						string tmp;
						switch(addr)
						{
							//Table 10
							case 2:
								if(current_byte & 0x40)
									tmp += string("SLP_S5#: ") + ((current_byte & 0x4)? "1" : "0") + "\n";
								if(current_byte & 0x20)
									tmp += string("SLP_S4#: ") + ((current_byte & 0x2)? "1" : "0") + "\n";
								if(current_byte & 0x10)
									tmp += string("SLP_S3#: ") + ((current_byte & 0x1)? "1" : "0") + "\n";
								break;

							//Table 11
							case 3:
								if(current_byte & 0x40)
									tmp += string("OOB_RST_WARN: ") + ((current_byte & 0x4)? "1" : "0") + "\n";
								if(current_byte & 0x20)
									tmp += string("PLTRST#: ") + ((current_byte & 0x2)? "1" : "0") + "\n";
								if(current_byte & 0x10)
									tmp += string("SUS_STAT#: ") + ((current_byte & 0x1)? "1" : "0") + "\n";
								break;

							//Table 12
							case 4:
								if(current_byte & 0x80)
									tmp += string("PME#: ") + ((current_byte & 0x8)? "1" : "0") + "\n";
								if(current_byte & 0x40)
									tmp += string("WAKE#: ") + ((current_byte & 0x4)? "1" : "0") + "\n";
								if(current_byte & 0x10)
									tmp += string("OOB_RST_ACK: ") + ((current_byte & 0x1)? "1" : "0") + "\n";
								break;

							//Table 13
							case 5:
								if(current_byte & 0x80)
									tmp += string("SLAVE_BOOT_LOAD_STATUS: ") + ((current_byte & 0x8)? "1" : "0") + "\n";
								if(current_byte & 0x40)
									tmp += string("ERROR_NONFATAL: ") + ((current_byte & 0x4)? "1" : "0") + "\n";
								if(current_byte & 0x20)
									tmp += string("ERROR_FATAL: ") + ((current_byte & 0x2)? "1" : "0") + "\n";
								if(current_byte & 0x10)
									tmp += string("SLAVE_BOOT_LOAD_DONE: ") + ((current_byte & 0x1)? "1" : "0") + "\n";
								break;

							//Table 14
							case 6:
								if(current_byte & 0x80)
									tmp += string("HOST_RST_ACK: ") + ((current_byte & 0x8)? "1" : "0") + "\n";
								if(current_byte & 0x40)
									tmp += string("RCIN#: ") + ((current_byte & 0x4)? "1" : "0") + "\n";
								if(current_byte & 0x20)
									tmp += string("SMI#: ") + ((current_byte & 0x2)? "1" : "0") + "\n";
								if(current_byte & 0x10)
									tmp += string("SCI#: ") + ((current_byte & 0x1)? "1" : "0") + "\n";
								break;

							//Table 15
							case 7:
								if(current_byte & 0x40)
									tmp += string("NMIOUT#: ") + ((current_byte & 0x4)? "1" : "0") + "\n";
								if(current_byte & 0x20)
									tmp += string("SMIOUT#: ") + ((current_byte & 0x2)? "1" : "0") + "\n";
								if(current_byte & 0x10)
									tmp += string("HOST_RST_WARN: ") + ((current_byte & 0x1)? "1" : "0") + "\n";
								break;
						}

						pack->m_headers["Info"] += tmp;
					}

					//Indexes 8-73 are reserved
					else if(addr <= 63)
						pack->m_headers["Info"] += "Reserved index\n";

					//64-127 platform specific
					else if(addr <= 127)
						pack->m_headers["Info"] += "Platform specific\n";

					//128-255 GPIO expander TODO
					else
						pack->m_headers["Info"] += "GPIO expander decode not implemented\n";

					//TODO: handle PUT_VWIRE here
					if(count == 0)
					{
						//Remove trailing newline
						pack->m_headers["Info"] = Trim(pack->m_headers["Info"]);

						txn_state = TXN_STATE_STATUS;
						data = 0;
					}
					else
					{
						txn_state = TXN_STATE_VWIRE_INDEX;
						count --;
					}

					break;	//end TXN_STATE_VWIRE_DATA

				////////////////////////////////////////////////////////////////////////////////////////////////////////
				// Flash channel

				case TXN_STATE_FLASH_TYPE:
					pack->m_data.push_back(current_byte);

					cap->m_offsets.push_back(bytestart);
					cap->m_durations.push_back(timestamp - bytestart);
					cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_FLASH_REQUEST_TYPE, current_byte));
					txn_state = TXN_STATE_FLASH_TAG_LENHI;

					flash_type = (ESPISymbol::ESpiFlashType)current_byte;

					switch(flash_type)
					{
						case ESPISymbol::FLASH_ERASE:
							pack->m_headers["Info"] = "Erase";
							break;

						case ESPISymbol::FLASH_READ:
							pack->m_headers["Info"] = "Read";
							break;

						case ESPISymbol::FLASH_WRITE:
							pack->m_headers["Info"] = "Write";
							break;

						default:
							pack->m_headers["Info"] = "Unknown flash op";
							break;
					}

					break;	//end TXN_STATE_FLASH_TYPE

				case TXN_STATE_FLASH_TAG_LENHI:
					pack->m_data.push_back(current_byte);

					//Tag is high 4 bits
					cap->m_offsets.push_back(bytestart);
					cap->m_durations.push_back(timestamp - bytestart);
					cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_FLASH_REQUEST_TAG, current_byte >> 4));
					pack->m_headers["Tag"] = to_string(current_byte >> 4);

					//Low 4 bits of this byte are the high length bits
					data = current_byte & 0xf;

					txn_state = TXN_STATE_FLASH_LENLO;
					break;	//end TXN_STATE_FLASH_TAG_LENHI

				case TXN_STATE_FLASH_LENLO:
					pack->m_data.push_back(current_byte);

					//Save the rest of the length
					cap->m_offsets.push_back(bytestart);
					cap->m_durations.push_back(timestamp - bytestart);
					cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_FLASH_REQUEST_LEN, current_byte | data));

					pack->m_headers["Len"] = to_string(current_byte | data);

					//Get ready to read the address
					count = 0;
					data = 0;
					txn_state = TXN_STATE_FLASH_ADDR;

					break;	//end TXN_STATE_FLASH_LENLO

				case TXN_STATE_FLASH_ADDR:

					//Save start time
					if(count == 0)
					{
						tstart = bytestart;
						cap->m_offsets.push_back(tstart);
					}

					//Save address (MSB to LSB)
					data = (data << 8) | current_byte;
					count ++;

					//Add data
					if(count == 4)
					{
						cap->m_durations.push_back(timestamp - tstart);
						cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_FLASH_REQUEST_ADDR, data));

						//Don't report free space in the protocol analyzer
						//to save column space
						char tmp[32];
						snprintf(tmp, sizeof(tmp), "%08lx", data);
						pack->m_headers["Address"] = tmp;

						count = 0;
						data = 0;

						//TODO: flash writes
						if(flash_type == ESPISymbol::FLASH_WRITE)
							txn_state = TXN_STATE_IDLE;

						//Reads and erases are done after the address
						else
							txn_state = TXN_STATE_STATUS;
					}

					break;	//end TXN_STATE_FLASH_ADDR
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
			case ESPISymbol::TYPE_FLASH_REQUEST_TYPE:
			case ESPISymbol::TYPE_FLASH_REQUEST_LEN:
				return m_standardColors[COLOR_CONTROL];

			case ESPISymbol::TYPE_CAPS_ADDR:
			case ESPISymbol::TYPE_VWIRE_COUNT:
			case ESPISymbol::TYPE_VWIRE_INDEX:
			case ESPISymbol::TYPE_FLASH_REQUEST_TAG:
			case ESPISymbol::TYPE_FLASH_REQUEST_ADDR:
				return m_standardColors[COLOR_ADDRESS];

			case ESPISymbol::TYPE_COMMAND_CRC_GOOD:
			case ESPISymbol::TYPE_RESPONSE_CRC_GOOD:
				return m_standardColors[COLOR_CHECKSUM_OK];
			case ESPISymbol::TYPE_COMMAND_CRC_BAD:
			case ESPISymbol::TYPE_RESPONSE_CRC_BAD:
				return m_standardColors[COLOR_CHECKSUM_BAD];

			case ESPISymbol::TYPE_GENERAL_CAPS:
			case ESPISymbol::TYPE_CH1_CAPS_RD:
			case ESPISymbol::TYPE_CH1_CAPS_WR:
			case ESPISymbol::TYPE_CH2_CAPS_RD:
			case ESPISymbol::TYPE_CH2_CAPS_WR:
			case ESPISymbol::TYPE_VWIRE_DATA:
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
				return string("CRC: ") + to_string_hex(s.m_data);

			case ESPISymbol::TYPE_VWIRE_COUNT:
				return string("Count: ") + to_string(s.m_data + 1);

			case ESPISymbol::TYPE_VWIRE_INDEX:
				return string("Index: ") + to_string(s.m_data);

			case ESPISymbol::TYPE_VWIRE_DATA:
				snprintf(tmp, sizeof(tmp), "%02lx", s.m_data);
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

			case ESPISymbol::TYPE_GENERAL_CAPS:
				if(s.m_data & 0x80000000)
					stmp += "CRC checking enabled\n";
				if(s.m_data & 0x40000000)
					stmp += "Response modifier enabled\n";
				if( (s.m_data & 0x10000000) == 0)
					stmp += "DQ1 used as alert\n";
				else
					stmp += "ALERT# used as alert\n";
				switch( (s.m_data >> 26) & 0x3)
				{
					case 0:
						stmp += "x1 mode\n";
						break;
					case 1:
						stmp += "x2 mode\n";
						break;
					case 2:
						stmp += "x4 mode\n";
						break;
					default:
						stmp += "Invalid IO mode\n";
						break;
				}

				switch( (s.m_data >> 24) & 0x3)
				{
					case 0:
						stmp += "Supports x1 mode only\n";
						break;
					case 1:
						stmp += "Supports x1 and x2 modes\n";
						break;
					case 2:
						stmp += "Supports x1 and x4 modes\n";
						break;
					default:
						stmp += "Supports x1, x2, and x4 modes\n";
						break;
				}

				if(s.m_data & 0x00800000)
					stmp += "ALERT# configured as open drain\n";
				else
					stmp += "ALERT# configured as push-pull\n";

				switch( (s.m_data >> 20) & 0x7)
				{
					case 0:
						stmp += "20MHz SCK\n";
						break;
					case 1:
						stmp += "25MHz SCK\n";
						break;
					case 2:
						stmp += "33MHz SCK\n";
						break;
					case 3:
						stmp += "50MHz SCK\n";
						break;
					case 4:
						stmp += "66MHz SCK\n";
						break;
					default:
						stmp += "Invalid SCK speed\n";
						break;
				}

				if(s.m_data & 0x00080000)
					stmp += "ALERT# supports open drain mode\n";

				switch( (s.m_data >> 16) & 0x7)
				{
					case 0:
						stmp += "Max SCK: 20 MHz\n";
						break;
					case 1:
						stmp += "Max SCK: 25 MHz\n";
						break;
					case 2:
						stmp += "Max SCK: 33 MHz\n";
						break;
					case 3:
						stmp += "Max SCK: 50 MHz\n";
						break;
					case 4:
						stmp += "Max SCK: 66 MHz\n";
						break;
					default:
						stmp += "Invalid max SCK speed\n";
						break;
				}

				//15:12 = max wait states
				if( ( (s.m_data >> 12) & 0xf) == 0)
					stmp += "Max wait states: 16\n";
				else
					stmp += string("Max wait states: ") + to_string((s.m_data >> 12) & 0xf) + "\n";

				if(s.m_data & 0x80)
					stmp += "Platform channel 7 present\n";
				if(s.m_data & 0x40)
					stmp += "Platform channel 6 present\n";
				if(s.m_data & 0x20)
					stmp += "Platform channel 5 present\n";
				if(s.m_data & 0x10)
					stmp += "Platform channel 4 present\n";
				if(s.m_data & 0x08)
					stmp += "Flash channel present\n";
				if(s.m_data & 0x04)
					stmp += "OOB channel present\n";
				if(s.m_data & 0x02)
					stmp += "Virtual wire channel present\n";
				if(s.m_data & 0x01)
					stmp += "Peripheral channel present\n";
				return stmp;	//end TYPE_GENERAL_CAPS

			case ESPISymbol::TYPE_CH1_CAPS_RD:
				stmp += "Operating max vwires: ";
				stmp += to_string( ((s.m_data >> 16) & 0x3f) + 1) + "\n";

				stmp += "Max vwires supported: ";
				stmp += to_string( ((s.m_data >> 8) & 0x3f) + 1) + "\n";

				if(s.m_data & 2)
					stmp += "Ready\n";
				else
					stmp += "Not ready\n";

				if(s.m_data & 1)
					stmp += "Enabled\n";
				else
					stmp += "Disabled\n";

				return stmp;	//end TYPE_CH1_CAPS_RD

			case ESPISymbol::TYPE_CH1_CAPS_WR:
				stmp += "Operating max vwires: ";
				stmp += to_string( ((s.m_data >> 16) & 0x3f) + 1) + "\n";

				if(s.m_data & 1)
					stmp += "Enabled\n";
				else
					stmp += "Disabled\n";

				return stmp;	//end TYPE_CH1_CAPS_WR

			case ESPISymbol::TYPE_CH2_CAPS_RD:

				stmp += "Max OOB payload selected: ";
				switch( (s.m_data >> 8) & 0x7)
				{
					case 1:
						stmp += "64 bytes\n";
						break;
					case 2:
						stmp += "128 bytes\n";
						break;
					case 3:
						stmp += "256 bytes\n";
						break;
					default:
						stmp += "Reserved\n";
						break;
				}

				stmp += "Max OOB payload supported: ";
				switch( (s.m_data >> 4) & 0x7)
				{
					case 1:
						stmp += "64 bytes\n";
						break;
					case 2:
						stmp += "128 bytes\n";
						break;
					case 3:
						stmp += "256 bytes\n";
						break;
					default:
						stmp += "Reserved\n";
						break;
				}

				if(s.m_data & 2)
					stmp += "OOB channel ready\n";
				else
					stmp += "OOB channel not ready\n";

				if(s.m_data & 1)
					stmp += "OOB channel enabled\n";
				else
					stmp += "OOB channel disabled\n";

				return stmp;	//end TYPE_CH2_CAPS_RD

			case ESPISymbol::TYPE_CH2_CAPS_WR:

				stmp += "Max OOB payload selected: ";
				switch( (s.m_data >> 8) & 0x7)
				{
					case 1:
						stmp += "64 bytes\n";
						break;
					case 2:
						stmp += "128 bytes\n";
						break;
					case 3:
						stmp += "256 bytes\n";
						break;
					default:
						stmp += "Reserved\n";
						break;
				}

				if(s.m_data & 1)
					stmp += "OOB channel enabled\n";
				else
					stmp += "OOB channel disabled\n";

				return stmp;	//end TYPE_CH2_CAPS_WR

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

			case ESPISymbol::TYPE_FLASH_REQUEST_TYPE:
				switch(s.m_data)
				{
					case ESPISymbol::FLASH_READ:
						return "Read";
					case ESPISymbol::FLASH_WRITE:
						return "Write";
					case ESPISymbol::FLASH_ERASE:
						return "Erase";
				}
				break;

			case ESPISymbol::TYPE_FLASH_REQUEST_TAG:
				return string("Tag: ") + to_string(s.m_data);

			case ESPISymbol::TYPE_FLASH_REQUEST_LEN:
				return string("Len: ") + to_string(s.m_data);

			case ESPISymbol::TYPE_FLASH_REQUEST_ADDR:
				snprintf(tmp, sizeof(tmp), "Addr: %08lx", s.m_data);
				return tmp;

			case ESPISymbol::TYPE_ERROR:
			default:
				return "ERROR";
		}
	}
	return "";
}

bool ESPIDecoder::CanMerge(Packet* first, Packet* /*cur*/, Packet* next)
{
	//Merge a "Get Status" with subsequent "Get Flash Non-Posted"
	if( (first->m_headers["Command"] == "Get Status") &&
		(first->m_headers["Status"].find("FLASH_NP_AVAIL") != string::npos) &&
		(next->m_headers["Command"] == "Get Flash Non-Posted") )
	{
		return true;
	}

	return false;
}

Packet* ESPIDecoder::CreateMergedHeader(Packet* pack, size_t i)
{
	Packet* ret = new Packet;
	ret->m_offset = pack->m_offset;
	ret->m_len = pack->m_len;			//TODO: extend?

	Packet* first = m_packets[i];

	if(first->m_headers["Command"] == "Get Status")
	{
		//Look up the second packet in the string
		if(i+1 < m_packets.size())
		{
			Packet* second = m_packets[i+1];

			//It's a flash transaction
			if(second->m_headers["Command"] == "Get Flash Non-Posted")
			{
				ret->m_headers["Address"] = second->m_headers["Address"];
				ret->m_headers["Len"] = second->m_headers["Len"];
				ret->m_headers["Tag"] = second->m_headers["Tag"];

				if(second->m_headers["Info"] == "Read")
				{
					ret->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
					ret->m_headers["Command"] = "Flash Read";
				}
				else if(second->m_headers["Info"] == "Write")
				{
					ret->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
					ret->m_headers["Command"] = "Flash Write";
				}
				else if(second->m_headers["Info"] == "Erase")
				{
					ret->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
					ret->m_headers["Command"] = "Flash Erase";
				}
			}
		}
	}

	return ret;
}
