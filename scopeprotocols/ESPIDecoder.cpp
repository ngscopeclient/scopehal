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
#include "ESPIDecoder.h"
#include "SPIDecoder.h"

#include <cinttypes>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ESPIDecoder::ESPIDecoder(const string& color)
	: PacketDecoder(color, CAT_BUS)
	, m_busWidthName("Bus Width")
{
	CreateInput("clk");
	CreateInput("cs#");
	CreateInput("dq3");
	CreateInput("dq2");
	CreateInput("dq1");
	CreateInput("dq0");

	m_parameters[m_busWidthName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_busWidthName].AddEnumValue("x1", BUS_WIDTH_X1);
	m_parameters[m_busWidthName].AddEnumValue("x4", BUS_WIDTH_X4);
	m_parameters[m_busWidthName].AddEnumValue("Auto", BUS_WIDTH_AUTO);
	m_parameters[m_busWidthName].SetIntVal(BUS_WIDTH_AUTO);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool ESPIDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 6) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string ESPIDecoder::GetProtocolName()
{
	return "Intel eSPI";
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
	auto clk = GetInputWaveform(0);
	auto csn = GetInputWaveform(1);
	auto data3 = GetInputWaveform(2);
	auto data2 = GetInputWaveform(3);
	auto data1 = GetInputWaveform(4);
	auto data0 = GetInputWaveform(5);
	clk->PrepareForCpuAccess();
	csn->PrepareForCpuAccess();
	data3->PrepareForCpuAccess();
	data2->PrepareForCpuAccess();
	data1->PrepareForCpuAccess();
	data0->PrepareForCpuAccess();

	auto sclk = dynamic_cast<SparseDigitalWaveform*>(clk);
	auto uclk = dynamic_cast<UniformDigitalWaveform*>(clk);
	auto scsn = dynamic_cast<SparseDigitalWaveform*>(csn);
	auto ucsn = dynamic_cast<UniformDigitalWaveform*>(csn);
	auto sdata0 = dynamic_cast<SparseDigitalWaveform*>(data0);
	auto udata0 = dynamic_cast<UniformDigitalWaveform*>(data0);
	auto sdata1 = dynamic_cast<SparseDigitalWaveform*>(data1);
	auto udata1 = dynamic_cast<UniformDigitalWaveform*>(data1);
	auto sdata2 = dynamic_cast<SparseDigitalWaveform*>(data2);
	auto udata2 = dynamic_cast<UniformDigitalWaveform*>(data2);
	auto sdata3 = dynamic_cast<SparseDigitalWaveform*>(data3);
	auto udata3 = dynamic_cast<UniformDigitalWaveform*>(data3);

	size_t clklen = clk->size();
	size_t cslen = csn->size();
	size_t datalen[4] =
	{
		data0->size(),
		data1->size(),
		data2->size(),
		data3->size()
	};

	//Figure out the bus width to use for protocol decoding
	auto busWidthMode = static_cast<BusWidth>(m_parameters[m_busWidthName].GetIntVal());
	BusWidth busWidthModeNext = busWidthMode;
	bool busWidthModeChanged = false;

	size_t ics			= 0;
	size_t iclk			= 0;
	size_t idata[4]		= {0};
	int64_t timestamp	= 0;

	//Create the waveform. Call SetData() early on so we can use GetText() in the packet decode
	auto cap = new ESPIWaveform;
	cap->m_timescale = clk->m_timescale;
	cap->m_startTimestamp = clk->m_startTimestamp;
	cap->m_startFemtoseconds = clk->m_startFemtoseconds;
	cap->m_triggerPhase = clk->m_triggerPhase;
	cap->PrepareForCpuAccess();
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
		TXN_STATE_FLASH_ADDR,
		TXN_STATE_FLASH_DATA,

		TXN_STATE_SMBUS_TYPE,
		TXN_STATE_SMBUS_TAG_LENHI,
		TXN_STATE_SMBUS_LENLO,
		TXN_STATE_SMBUS_ADDR,
		TXN_STATE_SMBUS_DATA,

		TXN_STATE_IOWR_ADDR,
		TXN_STATE_IOWR_DATA,

		TXN_STATE_IORD_ADDR,

		TXN_STATE_COMPLETION_TYPE,
		TXN_STATE_COMPLETION_TAG_LENHI,
		TXN_STATE_COMPLETION_LENLO,
		TXN_STATE_COMPLETION_DATA

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
	size_t payload_len = 0;

	int skip_bits			= 0;
	bool skip_next_falling	= false;
	int bitcount			= 0;
	int64_t bytestart		= 0;
	uint8_t current_byte	= 0;
	bool byte_valid_next	= false;
	ESPISymbol::ESpiCompletion completion_type	= ESPISymbol::COMPLETION_NONE;
	ESPISymbol::ESpiCycleType cycle_type = ESPISymbol::CYCLE_READ;
	while(true)
	{
		bool cur_cs = GetValue(scsn, ucsn, ics);
		bool cur_clk = GetValue(sclk, uclk, iclk);

		uint8_t cur_data =
			(GetValue(sdata3, udata3, idata[3]) ? 0x8 : 0) |
			(GetValue(sdata2, udata2, idata[2]) ? 0x4 : 0) |
			(GetValue(sdata1, udata1, idata[1]) ? 0x2 : 0) |
			(GetValue(sdata0, udata0, idata[0]) ? 0x1 : 0);

		bool byte_valid = false;

		string stmp;
		char tmp[128];

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

						//Figure out read mode for this byte
						if(bitcount == 0)
						{
							switch(busWidthMode)
							{
								case BUS_WIDTH_X1:
									break;
								case BUS_WIDTH_X4:
									if( (read_mode == READ_SI) || (read_mode == READ_SO) )
										read_mode = READ_QUAD_RISING;
									break;

								//If this is the beginning of a byte, see if either DQ2 or DQ3 is low.
								//This means they're actively driven (since they have pullups) and means
								//that we're definitely in quad mode.
								case BUS_WIDTH_AUTO:
								default:
									if( (cur_data & 0xc) != 0xc)
										read_mode = READ_QUAD_RISING;
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
				pack->m_len = (timestamp * clk->m_timescale) + clk->m_triggerPhase - pack->m_offset;
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
					pack->m_offset = bytestart * clk->m_timescale + clk->m_triggerPhase;
					cap->m_offsets.push_back(bytestart);
					cap->m_durations.push_back(timestamp - bytestart);
					cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_COMMAND_TYPE, current_byte));
					pack->m_headers["Command"] = cap->GetText(cap->m_samples.size()-1);

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

						//Expect a full flash completion
						case ESPISymbol::COMMAND_PUT_FLASH_C:
							txn_state = TXN_STATE_FLASH_TYPE;
							break;

						//Expect an OOB message
						case ESPISymbol::COMMAND_PUT_OOB:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							txn_state = TXN_STATE_SMBUS_TYPE;
							break;

						//Expect a virtual wire write packet
						case ESPISymbol::COMMAND_PUT_VWIRE:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
							txn_state = TXN_STATE_VWIRE_COUNT;
							break;

						//Expect a 16-bit address followed by 1-4 bytes of data
						case ESPISymbol::COMMAND_PUT_IOWR_SHORT_x1:
							payload_len = 1;
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
							txn_state = TXN_STATE_IOWR_ADDR;
							break;
						case ESPISymbol::COMMAND_PUT_IOWR_SHORT_x2:
							payload_len = 2;
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
							txn_state = TXN_STATE_IOWR_ADDR;
							break;
						case ESPISymbol::COMMAND_PUT_IOWR_SHORT_x4:
							payload_len = 4;
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
							txn_state = TXN_STATE_IOWR_ADDR;
							break;

						//Expect a 16 bit address
						case ESPISymbol::COMMAND_PUT_IORD_SHORT_x1:
							pack->m_headers["Len"] = "1";
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							txn_state = TXN_STATE_IORD_ADDR;
							break;

						case ESPISymbol::COMMAND_PUT_IORD_SHORT_x2:
							pack->m_headers["Len"] = "2";
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							txn_state = TXN_STATE_IORD_ADDR;
							break;

						case ESPISymbol::COMMAND_PUT_IORD_SHORT_x4:
							pack->m_headers["Len"] = "4";
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							txn_state = TXN_STATE_IORD_ADDR;
							break;

						//No arguments
						case ESPISymbol::COMMAND_GET_STATUS:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_STATUS];
							txn_state = TXN_STATE_COMMAND_CRC8;
							break;
						case ESPISymbol::COMMAND_GET_FLASH_NP:
						case ESPISymbol::COMMAND_GET_PC:
							txn_state = TXN_STATE_COMMAND_CRC8;
							break;
						case ESPISymbol::COMMAND_GET_VWIRE:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							txn_state = TXN_STATE_COMMAND_CRC8;
							break;
						case ESPISymbol::COMMAND_GET_OOB:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
							txn_state = TXN_STATE_COMMAND_CRC8;
							break;
						case ESPISymbol::COMMAND_RESET:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_COMMAND];
							txn_state = TXN_STATE_COMMAND_CRC8;
							break;

						//TODO
						case ESPISymbol::COMMAND_PUT_PC:
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

					//Expect a response after a 2-cycle bus turnaround
					txn_state = TXN_STATE_RESPONSE;
					skip_bits = 2;

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
						pack->m_headers["Address"] = cap->GetText(cap->m_samples.size()-1);

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
									case 0x8:
										cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_GENERAL_CAPS_WR, data));
										pack->m_headers["Info"] = Trim(cap->GetText(cap->m_samples.size()-1));

										//General Capabilities register includes the I/O bus width flag
										//Decode writes and update our bus width for correct decode of the next packet
										//Note that the change is deferred: we still use the current bus width mode
										//to decode the CRC and remaining fields of THIS packet.
										switch( (data >> 26) & 0x3)
										{
											case 0:
												busWidthModeNext = BUS_WIDTH_X1;
												busWidthModeChanged = true;
												break;

											case 1:
												LogWarning("x2 mode not implemented\n");
												break;

											case 2:
												busWidthModeNext = BUS_WIDTH_X4;
												busWidthModeChanged = true;
												break;

											default:
												LogWarning("Invalid IO mode\n");
												break;
										}
										break;

									case 0x10:
										cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_CH0_CAPS_WR, data));
										pack->m_headers["Info"] = Trim(cap->GetText(cap->m_samples.size()-1));
										break;

									case 0x20:
										cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_CH1_CAPS_WR, data));
										pack->m_headers["Info"] = Trim(cap->GetText(cap->m_samples.size()-1));
										break;

									case 0x30:
										cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_CH2_CAPS_WR, data));
										pack->m_headers["Info"] = Trim(cap->GetText(cap->m_samples.size()-1));
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

					//Handle wait states
					if( (current_byte & 0xcf) == 0x0f)
					{
						//Merge consecutive wait states
						size_t last = cap->m_samples.size() - 1;
						if(cap->m_samples[last].m_type == ESPISymbol::TYPE_WAIT)
							cap->m_durations[last] = timestamp - cap->m_offsets[last];
						else
						{
							cap->m_offsets.push_back(bytestart);
							cap->m_durations.push_back(timestamp - bytestart);
							cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_WAIT));
						}
					}
					else
					{
						//Start with a fresh CRC for the response phase
						crc = 0;

						cap->m_offsets.push_back(bytestart);
						cap->m_durations.push_back(timestamp - bytestart);
						cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_RESPONSE_OP, current_byte));

						//TODO: support appended completions
						completion_type = static_cast<ESPISymbol::ESpiCompletion>(current_byte >> 6);
						if(completion_type != ESPISymbol::COMPLETION_NONE)
							LogWarning("Appended completions not implemented yet\n");

						pack->m_headers["Response"] = cap->GetText(cap->m_samples.size()-1);

						count = 0;
						data = 0;

						switch(current_cmd)
						{
							case ESPISymbol::COMMAND_GET_CONFIGURATION:
								txn_state = TXN_STATE_RESPONSE_DATA;
								break;

							case ESPISymbol::COMMAND_GET_VWIRE:
								txn_state = TXN_STATE_VWIRE_COUNT;
								break;

							case ESPISymbol::COMMAND_GET_FLASH_NP:
								txn_state = TXN_STATE_FLASH_TYPE;
								break;

							case ESPISymbol::COMMAND_GET_OOB:
								txn_state = TXN_STATE_SMBUS_TYPE;
								break;

							case ESPISymbol::COMMAND_GET_PC:
								txn_state = TXN_STATE_COMPLETION_TYPE;
								break;

							case ESPISymbol::COMMAND_GET_STATUS:
							default:
								txn_state = TXN_STATE_STATUS;
								break;
						}
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
										cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_GENERAL_CAPS_RD, data));
										pack->m_headers["Info"] = Trim(cap->GetText(cap->m_samples.size()-1));
										break;

									case 0x10:
										cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_CH0_CAPS_RD, data));
										pack->m_headers["Info"] = Trim(cap->GetText(cap->m_samples.size()-1));
										break;

									case 0x20:
										cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_CH1_CAPS_RD, data));
										pack->m_headers["Info"] = Trim(cap->GetText(cap->m_samples.size()-1));
										break;

									case 0x30:
										cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_CH2_CAPS_RD, data));
										pack->m_headers["Info"] = Trim(cap->GetText(cap->m_samples.size()-1));
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
						if(data & 0x2000)
							stmp += "FLASH_NP_AVAIL ";
						if(data & 0x1000)
							stmp += "FLASH_C_AVAIL ";
						if(data & 0x0200)
							stmp += "FLASH_NP_FREE ";
						if(data & 0x0080)
							stmp += "OOB_AVAIL ";
						if(data & 0x0040)
							stmp += "VWIRE_AVAIL ";
						if(data & 0x0020)
							stmp += "NP_AVAIL ";
						if(data & 0x0010)
							stmp += "PC_AVAIL ";
						pack->m_headers["Status"] = stmp;

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

					//Commit bus width changes before the next packet
					if(busWidthModeChanged)
						busWidthMode = busWidthModeNext;

					//Done with the packet
					txn_state = TXN_STATE_IDLE;
					break;	//end TXN_STATE_RESPONSE_CRC8

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
						stmp = "IRQ";
						if(addr == 0)
							stmp += to_string(current_byte & 0x7f);
						else
							stmp += to_string( (current_byte & 0x7f) + 128 );
						if(current_byte & 0x80)
							stmp += " high\n";
						else
							stmp += " low\n";

						pack->m_headers["Info"] += stmp;
					}

					//Indexes 2-7 are "system events".
					//See table 10-15
					else if(addr <= 7)
					{
						switch(addr)
						{
							//Table 10
							case 2:
								if(current_byte & 0x40)
									stmp += string("SLP_S5#: ") + ((current_byte & 0x4)? "1" : "0") + "\n";
								if(current_byte & 0x20)
									stmp += string("SLP_S4#: ") + ((current_byte & 0x2)? "1" : "0") + "\n";
								if(current_byte & 0x10)
									stmp += string("SLP_S3#: ") + ((current_byte & 0x1)? "1" : "0") + "\n";
								break;

							//Table 11
							case 3:
								if(current_byte & 0x40)
									stmp += string("OOB_RST_WARN: ") + ((current_byte & 0x4)? "1" : "0") + "\n";
								if(current_byte & 0x20)
									stmp += string("PLTRST#: ") + ((current_byte & 0x2)? "1" : "0") + "\n";
								if(current_byte & 0x10)
									stmp += string("SUS_STAT#: ") + ((current_byte & 0x1)? "1" : "0") + "\n";
								break;

							//Table 12
							case 4:
								if(current_byte & 0x80)
									stmp += string("PME#: ") + ((current_byte & 0x8)? "1" : "0") + "\n";
								if(current_byte & 0x40)
									stmp += string("WAKE#: ") + ((current_byte & 0x4)? "1" : "0") + "\n";
								if(current_byte & 0x10)
									stmp += string("OOB_RST_ACK: ") + ((current_byte & 0x1)? "1" : "0") + "\n";
								break;

							//Table 13
							case 5:
								if(current_byte & 0x80)
									stmp += string("SLAVE_BOOT_LOAD_STATUS: ") + ((current_byte & 0x8)? "1" : "0") + "\n";
								if(current_byte & 0x40)
									stmp += string("ERROR_NONFATAL: ") + ((current_byte & 0x4)? "1" : "0") + "\n";
								if(current_byte & 0x20)
									stmp += string("ERROR_FATAL: ") + ((current_byte & 0x2)? "1" : "0") + "\n";
								if(current_byte & 0x10)
									stmp += string("SLAVE_BOOT_LOAD_DONE: ") + ((current_byte & 0x1)? "1" : "0") + "\n";
								break;

							//Table 14
							case 6:
								if(current_byte & 0x80)
									stmp += string("HOST_RST_ACK: ") + ((current_byte & 0x8)? "1" : "0") + "\n";
								if(current_byte & 0x40)
									stmp += string("RCIN#: ") + ((current_byte & 0x4)? "1" : "0") + "\n";
								if(current_byte & 0x20)
									stmp += string("SMI#: ") + ((current_byte & 0x2)? "1" : "0") + "\n";
								if(current_byte & 0x10)
									stmp += string("SCI#: ") + ((current_byte & 0x1)? "1" : "0") + "\n";
								break;

							//Table 15
							case 7:
								if(current_byte & 0x40)
									stmp += string("NMIOUT#: ") + ((current_byte & 0x4)? "1" : "0") + "\n";
								if(current_byte & 0x20)
									stmp += string("SMIOUT#: ") + ((current_byte & 0x2)? "1" : "0") + "\n";
								if(current_byte & 0x10)
									stmp += string("HOST_RST_WARN: ") + ((current_byte & 0x1)? "1" : "0") + "\n";
								break;
						}

						pack->m_headers["Info"] += stmp;
					}

					//Indexes 8-73 are reserved
					else if(addr <= 63)
						pack->m_headers["Info"] += "Reserved index\n";

					//64-127 platform specific
					else if(addr <= 127)
					{
						snprintf(tmp, sizeof(tmp), "Platform specific %02" PRIx64 ":%02x\n", addr, current_byte);
						pack->m_headers["Info"] += tmp;
					}

					//128-255 GPIO expander TODO
					else
						pack->m_headers["Info"] += "GPIO expander decode not implemented\n";

					if(count == 0)
					{
						//Remove trailing newline
						pack->m_headers["Info"] = Trim(pack->m_headers["Info"]);

						if(current_cmd == ESPISymbol::COMMAND_PUT_VWIRE)
							txn_state = TXN_STATE_COMMAND_CRC8;
						else
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

					cycle_type = (ESPISymbol::ESpiCycleType)current_byte;

					switch(cycle_type)
					{
						case ESPISymbol::CYCLE_ERASE:
							pack->m_headers["Info"] = "Erase";
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
							break;

						case ESPISymbol::CYCLE_READ:
							pack->m_headers["Info"] = "Read";
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							break;

						case ESPISymbol::CYCLE_WRITE:
							pack->m_headers["Info"] = "Write";
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
							break;

						case ESPISymbol::CYCLE_SUCCESS_DATA_FIRST:
						case ESPISymbol::CYCLE_SUCCESS_DATA_MIDDLE:
						case ESPISymbol::CYCLE_SUCCESS_DATA_LAST:
						case ESPISymbol::CYCLE_SUCCESS_DATA_ONLY:
							pack->m_headers["Info"] = "Read Data";
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
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
					cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_REQUEST_TAG, current_byte >> 4));
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
					payload_len = current_byte | data;
					cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_REQUEST_LEN, payload_len));

					pack->m_headers["Len"] = to_string(payload_len);

					//Get ready to read the address or data
					count = 0;
					data = 0;

					if(cycle_type >= ESPISymbol::CYCLE_SUCCESS_NODATA)
					{
						pack->m_data.clear();
						txn_state = TXN_STATE_FLASH_DATA;
					}
					else
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
						snprintf(tmp, sizeof(tmp), "%08" PRIx64, data);
						pack->m_headers["Address"] = tmp;

						count = 0;
						data = 0;

						//Write requests are followed by data
						if(cycle_type == ESPISymbol::CYCLE_WRITE)
						{
							pack->m_data.clear();
							txn_state = TXN_STATE_FLASH_DATA;
						}

						//Reads and erases are done after the address
						else
							txn_state = TXN_STATE_STATUS;
					}

					break;	//end TXN_STATE_FLASH_ADDR

				case TXN_STATE_FLASH_DATA:

					pack->m_data.push_back(current_byte);

					//Save the data byte
					cap->m_offsets.push_back(bytestart);
					cap->m_durations.push_back(timestamp - bytestart);
					cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_FLASH_REQUEST_DATA, current_byte));

					//See if we're done
					count ++;
					if(count >= payload_len)
					{
						count = 0;
						data = 0;

						//Completion? Done with command
						if(current_cmd == ESPISymbol::COMMAND_PUT_FLASH_C)
							txn_state = TXN_STATE_COMMAND_CRC8;

						//Request? Done with response
						else
							txn_state = TXN_STATE_STATUS;
					}

					break;	//end TXN_STATE_FLASH_DATA

				////////////////////////////////////////////////////////////////////////////////////////////////////////
				// OOB (tunneled SMBus) channel

				case TXN_STATE_SMBUS_TYPE:
					cap->m_offsets.push_back(bytestart);
					cap->m_durations.push_back(timestamp - bytestart);
					cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_SMBUS_REQUEST_TYPE, current_byte));
					txn_state = TXN_STATE_SMBUS_TAG_LENHI;

					//should always be CYCLE_SMBUS
					break;	//end TXN_STATE_SMBUS_TYPE

				case TXN_STATE_SMBUS_TAG_LENHI:

					//Tag is high 4 bits
					cap->m_offsets.push_back(bytestart);
					cap->m_durations.push_back(timestamp - bytestart);
					cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_REQUEST_TAG, current_byte >> 4));
					pack->m_headers["Tag"] = to_string(current_byte >> 4);

					//Low 4 bits of this byte are the high length bits
					data = current_byte & 0xf;

					txn_state = TXN_STATE_SMBUS_LENLO;
					break;	//end TXN_STATE_SMBUS_TAG_LENHI

				case TXN_STATE_SMBUS_LENLO:

					//Save the rest of the length
					cap->m_offsets.push_back(bytestart);
					cap->m_durations.push_back(timestamp - bytestart);
					payload_len = current_byte | data;
					cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_REQUEST_LEN, payload_len));

					pack->m_headers["Len"] = to_string(payload_len);

					txn_state = TXN_STATE_SMBUS_ADDR;

					break;	//end TXN_STATE_SMBUS_LENLO

				case TXN_STATE_SMBUS_ADDR:

					pack->m_data.clear();

					//Save the SMBus address
					cap->m_offsets.push_back(bytestart);
					cap->m_durations.push_back(timestamp - bytestart);
					cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_SMBUS_REQUEST_ADDR, current_byte));

					snprintf(tmp, sizeof(tmp), "%02x", current_byte);
					pack->m_headers["Address"] = tmp;

					//Get ready to read the packet data
					//We already read the first byte of the SMBus packet (the slave address)
					//so start count at 1.
					count = 1;
					data = 0;

					txn_state = TXN_STATE_SMBUS_DATA;

					break;	//end TXN_STATE_SMBUS_ADDR

				case TXN_STATE_SMBUS_DATA:

					//Save the data byte
					pack->m_data.push_back(current_byte);
					cap->m_offsets.push_back(bytestart);
					cap->m_durations.push_back(timestamp - bytestart);
					cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_SMBUS_REQUEST_DATA, current_byte));

					//See if we're done
					count ++;
					if(count >= payload_len)
					{
						count = 0;
						data = 0;

						//Completion? Done with command
						if(current_cmd == ESPISymbol::COMMAND_PUT_OOB)
							txn_state = TXN_STATE_COMMAND_CRC8;

						//Request? Done with response
						else
							txn_state = TXN_STATE_STATUS;
					}

					break;	//end TXN_STATE_FLASH_DATA

				////////////////////////////////////////////////////////////////////////////////////////////////////////
				// I/O channel

				case TXN_STATE_IOWR_ADDR:

					//Save start time
					if(count == 0)
					{
						tstart = bytestart;
						cap->m_offsets.push_back(tstart);
					}

					//Save address (MSB to LSB)
					addr = (data << 8) | current_byte;
					count ++;

					//Add data
					if(count == 2)
					{
						cap->m_durations.push_back(timestamp - tstart);
						cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_IO_ADDR, addr));

						snprintf(tmp, sizeof(tmp), "%04" PRIx64, addr);
						pack->m_headers["Address"] = tmp;

						pack->m_headers["Len"] = to_string(payload_len);

						count = 0;
						txn_state = TXN_STATE_IOWR_DATA;
					}

					break;	//end TXN_STATE_IOWR_ADDR

				case TXN_STATE_IOWR_DATA:

					//Save the data byte
					pack->m_data.push_back(current_byte);
					cap->m_offsets.push_back(bytestart);
					cap->m_durations.push_back(timestamp - bytestart);
					cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_SMBUS_REQUEST_DATA, current_byte));

					//See if we're done
					count ++;
					if(count >= payload_len)
					{
						count = 0;
						data = 0;
						txn_state = TXN_STATE_COMMAND_CRC8;
					}

					break;	//end TXN_STATE_IOWR_DATA

				case TXN_STATE_IORD_ADDR:

					//Save start time
					if(count == 0)
					{
						tstart = bytestart;
						cap->m_offsets.push_back(tstart);
					}

					//Save address (MSB to LSB)
					addr = (data << 8) | current_byte;
					count ++;

					//Add data
					if(count == 2)
					{
						cap->m_durations.push_back(timestamp - tstart);
						cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_IO_ADDR, addr));

						snprintf(tmp, sizeof(tmp), "%04" PRIx64, addr);
						pack->m_headers["Address"] = tmp;

						count = 0;
						txn_state = TXN_STATE_COMMAND_CRC8;
					}

					break;	//end TXN_STATE_IORD_ADDR

				////////////////////////////////////////////////////////////////////////////////////////////////////////
				// Completions

				case TXN_STATE_COMPLETION_TYPE:

					cap->m_offsets.push_back(bytestart);
					cap->m_durations.push_back(timestamp - bytestart);
					cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_COMPLETION_TYPE, current_byte));

					switch(current_byte)
					{
						case ESPISymbol::CYCLE_SUCCESS_NODATA:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_STATUS];
							break;

						case ESPISymbol::CYCLE_SUCCESS_DATA_MIDDLE:
						case ESPISymbol::CYCLE_SUCCESS_DATA_FIRST:
						case ESPISymbol::CYCLE_SUCCESS_DATA_LAST:
						case ESPISymbol::CYCLE_SUCCESS_DATA_ONLY:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							break;

						case ESPISymbol::CYCLE_FAIL_LAST:
						case ESPISymbol::CYCLE_FAIL_ONLY:
						default:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
							break;
					}

					txn_state = TXN_STATE_COMPLETION_TAG_LENHI;

					break;	//end TXN_STATE_COMPLETION_TYPE

				case TXN_STATE_COMPLETION_TAG_LENHI:

					//Tag is high 4 bits
					cap->m_offsets.push_back(bytestart);
					cap->m_durations.push_back(timestamp - bytestart);
					cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_REQUEST_TAG, current_byte >> 4));
					pack->m_headers["Tag"] = to_string(current_byte >> 4);

					//Low 4 bits of this byte are the high length bits
					data = current_byte & 0xf;

					txn_state = TXN_STATE_COMPLETION_LENLO;
					break;	//end TXN_STATE_COMPLETION_TAG_LENHI

				case TXN_STATE_COMPLETION_LENLO:

					//Save the rest of the length
					cap->m_offsets.push_back(bytestart);
					cap->m_durations.push_back(timestamp - bytestart);
					payload_len = current_byte | data;
					cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_REQUEST_LEN, payload_len));

					pack->m_headers["Len"] = to_string(payload_len);

					if(payload_len == 0)
						txn_state = TXN_STATE_STATUS;
					else
						txn_state = TXN_STATE_COMPLETION_DATA;

					break;	//end TXN_STATE_COMPLETION_LENLO

				case TXN_STATE_COMPLETION_DATA:

					//Save the data byte
					pack->m_data.push_back(current_byte);
					cap->m_offsets.push_back(bytestart);
					cap->m_durations.push_back(timestamp - bytestart);
					cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_COMPLETION_DATA, current_byte));

					//See if we're done
					count ++;
					if(count >= payload_len)
					{
						count = 0;
						data = 0;
						txn_state = TXN_STATE_STATUS;
					}

					break;	//end TXN_STATE_COMPLETION_DATA

			}

			//Checksum this byte
			crc = UpdateCRC8(crc, current_byte);

			bytestart = timestamp;
		}

		//Get timestamps of next event on each channel
		int64_t next_cs = GetNextEventTimestamp(scsn, ucsn, ics, cslen, timestamp);
		int64_t next_clk = GetNextEventTimestamp(sclk, uclk, iclk, clklen, timestamp);

		//If we can't move forward, stop (don't bother looking for glitches on data)
		int64_t next_timestamp = min(next_clk, next_cs);
		if(next_timestamp == timestamp)
			break;

		//All good, move on
		timestamp = next_timestamp;
		AdvanceToTimestamp(scsn, ucsn, ics, cslen, timestamp);
		AdvanceToTimestamp(sclk, uclk, iclk, clklen, timestamp);
		AdvanceToTimestamp(sdata0, udata0, idata[0], datalen[0], timestamp);
		AdvanceToTimestamp(sdata1, udata1, idata[1], datalen[1], timestamp);
		AdvanceToTimestamp(sdata2, udata2, idata[2], datalen[2], timestamp);
		AdvanceToTimestamp(sdata3, udata3, idata[3], datalen[3], timestamp);
	}

	cap->MarkModifiedFromCpu();
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

std::string ESPIWaveform::GetColor(size_t i)
{
	const ESPISymbol& s = m_samples[i];

	switch(s.m_type)
	{
		case ESPISymbol::TYPE_COMMAND_TYPE:
		case ESPISymbol::TYPE_RESPONSE_OP:
		case ESPISymbol::TYPE_RESPONSE_STATUS:
		case ESPISymbol::TYPE_FLASH_REQUEST_TYPE:
		case ESPISymbol::TYPE_REQUEST_LEN:
			return StandardColors::colors[StandardColors::COLOR_CONTROL];

		case ESPISymbol::TYPE_WAIT:
			return StandardColors::colors[StandardColors::COLOR_PREAMBLE];

		case ESPISymbol::TYPE_CAPS_ADDR:
		case ESPISymbol::TYPE_VWIRE_COUNT:
		case ESPISymbol::TYPE_VWIRE_INDEX:
		case ESPISymbol::TYPE_REQUEST_TAG:
		case ESPISymbol::TYPE_FLASH_REQUEST_ADDR:
		case ESPISymbol::TYPE_SMBUS_REQUEST_ADDR:
		case ESPISymbol::TYPE_IO_ADDR:
			return StandardColors::colors[StandardColors::COLOR_ADDRESS];

		case ESPISymbol::TYPE_COMMAND_CRC_GOOD:
		case ESPISymbol::TYPE_RESPONSE_CRC_GOOD:
			return StandardColors::colors[StandardColors::COLOR_CHECKSUM_OK];
		case ESPISymbol::TYPE_COMMAND_CRC_BAD:
		case ESPISymbol::TYPE_RESPONSE_CRC_BAD:
			return StandardColors::colors[StandardColors::COLOR_CHECKSUM_BAD];

		case ESPISymbol::TYPE_GENERAL_CAPS_RD:
		case ESPISymbol::TYPE_GENERAL_CAPS_WR:
		case ESPISymbol::TYPE_CH0_CAPS_RD:
		case ESPISymbol::TYPE_CH0_CAPS_WR:
		case ESPISymbol::TYPE_CH1_CAPS_RD:
		case ESPISymbol::TYPE_CH1_CAPS_WR:
		case ESPISymbol::TYPE_CH2_CAPS_RD:
		case ESPISymbol::TYPE_CH2_CAPS_WR:
		case ESPISymbol::TYPE_VWIRE_DATA:
		case ESPISymbol::TYPE_COMMAND_DATA_32:
		case ESPISymbol::TYPE_RESPONSE_DATA_32:
		case ESPISymbol::TYPE_FLASH_REQUEST_DATA:
		case ESPISymbol::TYPE_SMBUS_REQUEST_DATA:
		case ESPISymbol::TYPE_IO_DATA:
		case ESPISymbol::TYPE_COMPLETION_DATA:
			return StandardColors::colors[StandardColors::COLOR_DATA];

		case ESPISymbol::TYPE_SMBUS_REQUEST_TYPE:
			if(s.m_data == ESPISymbol::CYCLE_SMBUS)
				return StandardColors::colors[StandardColors::COLOR_CONTROL];
			else
				return StandardColors::colors[StandardColors::COLOR_ERROR];

		case ESPISymbol::TYPE_COMPLETION_TYPE:
			switch(s.m_data)
			{
				case ESPISymbol::CYCLE_SUCCESS_NODATA:
				case ESPISymbol::CYCLE_SUCCESS_DATA_MIDDLE:
				case ESPISymbol::CYCLE_SUCCESS_DATA_FIRST:
				case ESPISymbol::CYCLE_SUCCESS_DATA_LAST:
				case ESPISymbol::CYCLE_SUCCESS_DATA_ONLY:
					return StandardColors::colors[StandardColors::COLOR_CONTROL];

				case ESPISymbol::CYCLE_FAIL_LAST:
				case ESPISymbol::CYCLE_FAIL_ONLY:
				default:
					return StandardColors::colors[StandardColors::COLOR_ERROR];
			};
			break;

		default:
			return StandardColors::colors[StandardColors::COLOR_ERROR];
	}
}

string ESPIWaveform::GetText(size_t i)
{
	const ESPISymbol& s = m_samples[i];
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
					return "Get Posted Completion";
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
					snprintf(tmp, sizeof(tmp), "Unknown Cmd (%02" PRIx64 ")", s.m_data);
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
					snprintf(tmp, sizeof(tmp), "%04" PRIx64, s.m_data);
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
			return string("Index: ") + to_string_hex(s.m_data);

		case ESPISymbol::TYPE_VWIRE_DATA:
			snprintf(tmp, sizeof(tmp), "%02" PRIx64, s.m_data);
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
					snprintf(tmp, sizeof(tmp), "Unknown response %" PRIx64, s.m_data & 0xf);
					return tmp;
			}
			break;

		case ESPISymbol::TYPE_GENERAL_CAPS_RD:
		case ESPISymbol::TYPE_GENERAL_CAPS_WR:
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

			//read only, bits dontcare for write
			if(s.m_type == ESPISymbol::TYPE_GENERAL_CAPS_RD)
			{
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

			//read only, bits dontcare for write
			if(s.m_type == ESPISymbol::TYPE_GENERAL_CAPS_RD)
			{
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
			}

			//15:12 = max wait states
			if( ( (s.m_data >> 12) & 0xf) == 0)
				stmp += "Max wait states: 16\n";
			else
				stmp += string("Max wait states: ") + to_string((s.m_data >> 12) & 0xf) + "\n";

			//read only, bits dontcare for write
			if(s.m_type == ESPISymbol::TYPE_GENERAL_CAPS_RD)
			{
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
			}
			return stmp;	//end TYPE_GENERAL_CAPS

		case ESPISymbol::TYPE_CH0_CAPS_RD:

			if(s.m_data & 2)
				stmp += "Ready\n";
			else
				stmp += "Not ready\n";

			switch( (s.m_data >> 4) & 0x7)
			{
				case 1:
					stmp += "Max periph payload supported: 64\n";
					break;
				case 2:
					stmp += "Max periph payload supported: 128\n";
					break;
				case 3:
					stmp += "Max periph payload supported: 256\n";
					break;

				default:
					stmp += "Max periph payload supported: reserved\n";
					break;
			}

			//end CH0_CAPS_RD
			//fall through

		case ESPISymbol::TYPE_CH0_CAPS_WR:

			switch( (s.m_data >> 8) & 0x7)
			{
				case 1:
					stmp += "Max periph payload size: 64\n";
					break;
				case 2:
					stmp += "Max periph payload size: 128\n";
					break;
				case 3:
					stmp += "Max periph payload size: 256\n";
					break;

				default:
					stmp += "Max periph payload size: reserved\n";
					break;
			}

			switch( (s.m_data >> 12) & 0x7)
			{
				case 0:
					stmp += "Max periph read size: reserved\n";
					break;

				case 1:
					stmp += "Max periph read size: 64\n";
					break;
				case 2:
					stmp += "Max periph read size: 128\n";
					break;
				case 3:
					stmp += "Max periph read size: 256\n";
					break;
				case 4:
					stmp += "Max periph read size: 512\n";
					break;
				case 5:
					stmp += "Max periph read size: 1024\n";
					break;
				case 6:
					stmp += "Max periph read size: 2048\n";
					break;
				case 7:
					stmp += "Max periph read size: 4096\n";
					break;
			}

			if(s.m_data & 4)
				stmp += "Bus mastering enabled\n";
			else
				stmp += "Bus mastering disabled\n";

			if(s.m_data & 1)
				stmp += "Enabled\n";
			else
				stmp += "Disabled\n";

			return stmp;	//end TYPE_CH0_CAPS_WR

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

		case ESPISymbol::TYPE_RESPONSE_DATA_32:
		case ESPISymbol::TYPE_COMMAND_DATA_32:
			snprintf(tmp, sizeof(tmp), "%08" PRIx64, s.m_data);
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
				case ESPISymbol::CYCLE_READ:
					return "Read";
				case ESPISymbol::CYCLE_WRITE:
					return "Write";
				case ESPISymbol::CYCLE_ERASE:
					return "Erase";

				case ESPISymbol::CYCLE_SUCCESS_NODATA:
				case ESPISymbol::CYCLE_SUCCESS_DATA_FIRST:
				case ESPISymbol::CYCLE_SUCCESS_DATA_MIDDLE:
				case ESPISymbol::CYCLE_SUCCESS_DATA_LAST:
				case ESPISymbol::CYCLE_SUCCESS_DATA_ONLY:
					return "Success";
			}
			break;

		case ESPISymbol::TYPE_REQUEST_TAG:
			return string("Tag: ") + to_string(s.m_data);

		case ESPISymbol::TYPE_REQUEST_LEN:
			return string("Len: ") + to_string(s.m_data);

		case ESPISymbol::TYPE_FLASH_REQUEST_DATA:
		case ESPISymbol::TYPE_IO_DATA:
		case ESPISymbol::TYPE_COMPLETION_DATA:
			snprintf(tmp, sizeof(tmp), "%02" PRIx64, s.m_data);
			return tmp;

		case ESPISymbol::TYPE_FLASH_REQUEST_ADDR:
			snprintf(tmp, sizeof(tmp), "Addr: %08" PRIx64, s.m_data);
			return tmp;

		case ESPISymbol::TYPE_IO_ADDR:
			snprintf(tmp, sizeof(tmp), "Addr: %04" PRIx64, s.m_data);
			return tmp;

		case ESPISymbol::TYPE_SMBUS_REQUEST_ADDR:
			snprintf(tmp, sizeof(tmp), "Addr: %02" PRIx64, s.m_data);
			return tmp;

		case ESPISymbol::TYPE_SMBUS_REQUEST_TYPE:
			if(s.m_data == ESPISymbol::CYCLE_SMBUS)
				return "SMBus Msg";
			else
				return "Invalid";

		case ESPISymbol::TYPE_SMBUS_REQUEST_DATA:
			snprintf(tmp, sizeof(tmp), "%02" PRIx64, s.m_data);
			return tmp;

		case ESPISymbol::TYPE_COMPLETION_TYPE:
			switch(s.m_data)
			{
				case ESPISymbol::CYCLE_SUCCESS_NODATA:
				case ESPISymbol::CYCLE_SUCCESS_DATA_MIDDLE:
				case ESPISymbol::CYCLE_SUCCESS_DATA_FIRST:
				case ESPISymbol::CYCLE_SUCCESS_DATA_LAST:
				case ESPISymbol::CYCLE_SUCCESS_DATA_ONLY:
					return "Success";

				case ESPISymbol::CYCLE_FAIL_LAST:
				case ESPISymbol::CYCLE_FAIL_ONLY:
					return "Fail";

				default:
					return "ERROR";
			};
			break;

		case ESPISymbol::TYPE_WAIT:
			return "Wait";

		case ESPISymbol::TYPE_ERROR:
		default:
			return "ERROR";
	}

	//should never get here
	return "ERROR";
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

	//Merge a "Get Status" with subsequent "Put Flash Completion"
	//TODO: Only if the tags match!
	if( (first->m_headers["Command"] == "Get Status") &&
		(first->m_headers["Status"].find("FLASH_NP_AVAIL") != string::npos) &&
		(next->m_headers["Command"] == "Put Flash Completion") )
	{
		return true;
	}

	//Merge a "Get Status" with subsequent "Get OOB" or "Put OOB"
	//TODO: Only if the tags match!
	if( (first->m_headers["Command"] == "Get Status") &&
		(first->m_headers["Status"].find("OOB_AVAIL") != string::npos) &&
		(next->m_headers["Command"] == "Get OOB") )
	{
		return true;
	}
	if( (first->m_headers["Command"] == "Get Status") &&
		(first->m_headers["Status"].find("OOB_AVAIL") != string::npos) &&
		(next->m_headers["Command"] == "Put OOB") )
	{
		return true;
	}

	//Merge a "Get Status" with subsequent "Get Virtual Wire"
	if( (first->m_headers["Command"] == "Get Status") &&
		(first->m_headers["Status"].find("VWIRE_AVAIL") != string::npos) &&
		(next->m_headers["Command"] == "Get Virtual Wire") )
	{
		return true;
	}

	//Merge a "Put I/O Write" with subsequent "Get Status" and "Get Posted Completion"
	if( (first->m_headers["Command"] == "Put I/O Write") &&
		(next->m_headers["Command"] == "Get Status") &&
		(next->m_headers["Status"].find("PC_AVAIL") != string::npos) )
	{
		return true;
	}
	if( (first->m_headers["Command"] == "Put I/O Write") &&
		(next->m_headers["Command"] == "Get Posted Completion") )
	{
		return true;
	}

	//Merge a "Put I/O Read" with subsequent "Get Status" and "Get Posted Completion"
	if( (first->m_headers["Command"] == "Put I/O Read") &&
		(next->m_headers["Command"] == "Get Status") &&
		(next->m_headers["Status"].find("PC_AVAIL") != string::npos) )
	{
		return true;
	}
	if( (first->m_headers["Command"] == "Put I/O Read") &&
		(next->m_headers["Command"] == "Get Posted Completion") )
	{
		return true;
	}

	//Merge consecutive status register polls
	if( (first->m_headers["Command"] == "Get Configuration") &&
		(next->m_headers["Command"] == "Get Configuration") &&
		(first->m_headers["Address"] == next->m_headers["Address"]) )
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

	//Fetching commands requested by the peripheral
	if(first->m_headers["Command"] == "Get Status")
	{
		//Look up the second packet in the string
		if(i+1 < m_packets.size())
		{
			Packet* second = m_packets[i+1];

			ret->m_headers["Address"] = second->m_headers["Address"];
			ret->m_headers["Len"] = second->m_headers["Len"];
			ret->m_headers["Tag"] = second->m_headers["Tag"];

			//Flash transaction?
			if(second->m_headers["Command"] == "Get Flash Non-Posted")
			{
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

				//Append any flash completions we find
				//TODO: handle out-of-order here
				for(size_t j=i+2; j<m_packets.size(); j++)
				{
					Packet* p = m_packets[j];
					if(p->m_headers["Command"] != "Put Flash Completion")
						break;
					if(p->m_headers["Tag"] != second->m_headers["Tag"])
						break;

					for(auto b : p->m_data)
						ret->m_data.push_back(b);

					ret->m_len = p->m_offset + p->m_len - ret->m_offset;
				}
			}

			//SMBus transaction?
			else if(second->m_headers["Command"] == "Get OOB")
			{
				ret->m_headers["Command"] = "SMBus Access";
				ret->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
			}

			//Virtual Wire transaction?
			else if(second->m_headers["Command"] == "Get Virtual Wire")
			{
				ret->m_headers["Command"] = "Get Virtual Wire";
				ret->m_headers["Info"] = second->m_headers["Info"];
				ret->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
			}
		}
	}

	//Split transactions
	else if(first->m_headers["Command"] == "Put I/O Write")
	{
		ret->m_headers["Command"] = "I/O Write";
		ret->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
		ret->m_headers["Address"] = first->m_headers["Address"];
		ret->m_headers["Len"] = first->m_headers["Len"];

		//Get data from the write packet
		for(auto b : first->m_data)
			ret->m_data.push_back(b);

		//Get status from completions
		for(size_t j=i+1; j<m_packets.size(); j++)
		{
			Packet* p = m_packets[j];

			if(p->m_headers["Command"] == "Get Posted Completion")
				ret->m_headers["Response"] = p->m_headers["Response"];
			else if(p->m_headers["Command"] == "Get Status")
			{}
			else
				break;

			ret->m_len = p->m_offset + p->m_len - ret->m_offset;
		}
	}
	else if(first->m_headers["Command"] == "Put I/O Read")
	{
		ret->m_headers["Command"] = "I/O Read";
		ret->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
		ret->m_headers["Address"] = first->m_headers["Address"];
		ret->m_headers["Len"] = first->m_headers["Len"];

		//Get status and data from completions
		for(size_t j=i+1; j<m_packets.size(); j++)
		{
			Packet* p = m_packets[j];

			if(p->m_headers["Command"] == "Get Posted Completion")
				ret->m_headers["Response"] = p->m_headers["Response"];
			else if(p->m_headers["Command"] == "Get Status")
			{}
			else
				break;

			for(auto b : p->m_data)
				ret->m_data.push_back(b);

			ret->m_len = p->m_offset + p->m_len - ret->m_offset;
		}
	}

	//Status register polling
	else if(first->m_headers["Command"] == "Get Configuration")
	{
		ret->m_headers["Command"] = "Poll Configuration";
		ret->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_CONTROL];
		ret->m_headers["Address"] = first->m_headers["Address"];

		//Get status and data from completions
		size_t ilast = i;
		for(size_t j=i+1; j<m_packets.size(); j++)
		{
			Packet* p = m_packets[j];

			if( (p->m_headers["Command"] == "Get Configuration") &&
				(p->m_headers["Address"] == first->m_headers["Address"]) )
			{
				ilast = j;
			}
			else
				break;
		}

		Packet* last = m_packets[ilast];
		ret->m_headers["Len"] = to_string(ilast - i);
		ret->m_headers["Info"] = last->m_headers["Info"];
		ret->m_headers["Response"] = last->m_headers["Response"];
		for(auto b : last->m_data)
			ret->m_data.push_back(b);
		ret->m_len = last->m_offset + last->m_len - last->m_offset;
	}

	return ret;
}
