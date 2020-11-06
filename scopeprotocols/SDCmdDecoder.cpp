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
	@brief Implementation of SDCmdDecoder
 */

#include "../scopehal/scopehal.h"
#include "SDCmdDecoder.h"
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SDCmdDecoder::SDCmdDecoder(const string& color)
	: PacketDecoder(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_MEMORY)
	, m_cardtypename("Card Type")
{
	CreateInput("CMD");
	CreateInput("CLK");

	m_parameters[m_cardtypename] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_cardtypename].AddEnumValue("SD", SD_GENERIC);
	m_parameters[m_cardtypename].AddEnumValue("eMMC", SD_EMMC);
	m_parameters[m_cardtypename].SetIntVal(SD_GENERIC);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool SDCmdDecoder::NeedsConfig()
{
	return true;
}

string SDCmdDecoder::GetProtocolName()
{
	return "SD Card Command Bus";
}

bool SDCmdDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
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

void SDCmdDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "SDCmd(%s, %s)",
		GetInputDisplayName(0).c_str(), GetInputDisplayName(1).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void SDCmdDecoder::Refresh()
{
	ClearPackets();

	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Sample the input data
	auto cmd = GetDigitalInputWaveform(0);
	auto clk = GetDigitalInputWaveform(1);
	DigitalWaveform dcmd;
	SampleOnRisingEdges(cmd, clk, dcmd);
	size_t len = dcmd.m_samples.size();

	//Create the capture
	auto cap = new SDCmdWaveform;
	cap->m_timescale = 1;
	cap->m_startTimestamp = clk->m_startTimestamp;
	cap->m_startPicoseconds = clk->m_startPicoseconds;
	SetData(cap, 0);

	enum
	{
		STATE_IDLE,
		STATE_TYPE,
		STATE_COMMAND_HEADER,
		STATE_COMMAND_BODY,
		STATE_CRC,
		STATE_STOP,

		STATE_RESPONSE_HEADER,
		STATE_RESPONSE_BODY,

		STATE_HANG
	} state = STATE_IDLE;

	int64_t tstart = 0;
	int nbit = 0;
	uint32_t data = 0;
	uint32_t extdata[4] = {0};

	//Reference: SD Physical Layer Simplified Specification v.8.00

	int last_cmd = 0;
	bool app_cmd = false;
	Packet* pack = NULL;
	for(size_t i=0; i<len; i++)
	{
		bool b = dcmd.m_samples[i];
		int64_t off = dcmd.m_offsets[i];		//no need to multiply by timescale
		int64_t dur = dcmd.m_durations[i];		//because SampleOnRisingEdges() always uses 1ps timesteps
		int64_t end = off + dur;

		switch(state)
		{
			//Wait for a start bit
			case STATE_IDLE:
				if(!b)
				{
					tstart = off;
					state = STATE_TYPE;

					//Create a new packet. If we already have an incomplete one that got aborted, reset it
					if(pack)
					{
						pack->m_data.clear();
						pack->m_headers.clear();
					}
					else
						pack = new Packet;

					pack->m_offset = dcmd.m_offsets[i];
					pack->m_len = 0;
				}
				break;

			//Read the type bit
			//1 = command, 0 = response
			case STATE_TYPE:
				cap->m_offsets.push_back(tstart);
				cap->m_durations.push_back(end - tstart);
				cap->m_samples.push_back(SDCmdSymbol(SDCmdSymbol::TYPE_HEADER, b));

				tstart = end;
				nbit = 0;
				data = 0;
				if(b)
				{
					state = STATE_COMMAND_HEADER;
					pack->m_headers["Type"] = "Command";
				}
				else
				{
					state = STATE_RESPONSE_HEADER;
					pack->m_headers["Type"] = "Reply";
				}

				break;

			//Start a command or reply packet
			//Read command index (6 bits)
			case STATE_COMMAND_HEADER:
			case STATE_RESPONSE_HEADER:
				data = (data << 1) | b;
				nbit ++;
				if(nbit == 6)
				{
					cap->m_offsets.push_back(tstart);
					cap->m_durations.push_back(end - tstart);

					//If the last command was CMD55 (APP_CMD) then add 100.
					//We code ACMD1 as 101, etc.
					if(state == STATE_RESPONSE_HEADER)
					{
						if(app_cmd)
							data += 100;
					}
					else
					{
						if(data == 55)
							app_cmd = true;
						else if(app_cmd)
						{
							data += 100;
							app_cmd = false;
						}
					}

					//Save the command code so we know how to parse replies
					if(state == STATE_COMMAND_HEADER)
						last_cmd = data;

					cap->m_samples.push_back(SDCmdSymbol(SDCmdSymbol::TYPE_COMMAND, data));

					pack->m_headers["Command"] = GetText(cap->m_samples.size()-1);

					if(last_cmd >= 100)
						pack->m_headers["Code"] = string("ACMD") + to_string(last_cmd - 100);
					else
						pack->m_headers["Code"] = string("CMD") + to_string(last_cmd);

					//Set packet color based on command
					if(state == STATE_RESPONSE_HEADER)
						pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_STATUS];
					else
					{
						switch(data)
						{
							//WRITE_BLOCK, WRITE_MULTIPLE_BLOCK
							case 24:
							case 25:
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
								break;

							//READ_SINGLE_BLOCK, READ_MULTIPLE_BLOCK
							case 17:
							case 18:
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
								break;

							//Default everything else to "control"
							default:
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_CONTROL];
								break;
						}
					}

					data = 0;
					nbit = 0;
					tstart = end;
					if(state == STATE_COMMAND_HEADER)
						state = STATE_COMMAND_BODY;
					else
						state = STATE_RESPONSE_BODY;
				}
				break;

			//Read arguments for a command packet
			case STATE_COMMAND_BODY:
				data = (data << 1) | b;
				nbit ++;

				if(nbit == 32)
				{
					cap->m_offsets.push_back(tstart);
					cap->m_durations.push_back(end - tstart);
					cap->m_samples.push_back(SDCmdSymbol(SDCmdSymbol::TYPE_COMMAND_ARGS, data));

					data = 0;
					nbit = 0;
					tstart = end;
					state = STATE_CRC;

					pack->m_headers["Info"] = GetText(cap->m_samples.size()-1);
				}
				break;

			//Read arguments for a response packet
			case STATE_RESPONSE_BODY:
				{
					//Figure out the expected reply format (4.7.4)
					data = (data << 1) | b;
					nbit ++;

					//CMD2 has a 128-bit response with no CRC
					if(last_cmd == 2)
					{
						if(nbit == 32)
							extdata[0] = data;
						if(nbit == 64)
							extdata[1] = data;
						if(nbit == 96)
							extdata[2] = data;
						if(nbit == 128)
						{
							extdata[3] = data;

							cap->m_offsets.push_back(tstart);
							cap->m_durations.push_back(end - tstart);
							cap->m_samples.push_back(SDCmdSymbol(SDCmdSymbol::TYPE_RESPONSE_ARGS,
								extdata[0], extdata[1], extdata[2], extdata[3]));

							pack->m_headers["Info"] = GetText(cap->m_samples.size()-1);

							//no CRC
							//stop bit is parsed as last data bit
							state = STATE_IDLE;

							//end the packet now
							m_packets.push_back(pack);
							pack = NULL;
						}
					}
					else if(nbit == 32)
					{
						cap->m_offsets.push_back(tstart);
						cap->m_durations.push_back(end - tstart);
						cap->m_samples.push_back(SDCmdSymbol(SDCmdSymbol::TYPE_RESPONSE_ARGS, data));

						data = 0;
						nbit = 0;
						tstart = end;
						state = STATE_CRC;

						pack->m_headers["Info"] = GetText(cap->m_samples.size()-1);
					}

				}
				break;

			//Reads the CRC
			//ACMD41 response always has 0x7F here for some reason and not a real CRC (4.9.4)
			case STATE_CRC:
				data = (data << 1) | b;
				nbit ++;
				if(nbit == 7)
				{
					//TODO: verify the CRC
					cap->m_offsets.push_back(tstart);
					cap->m_durations.push_back(end - tstart);
					cap->m_samples.push_back(SDCmdSymbol(SDCmdSymbol::TYPE_CRC_OK, data));

					state = STATE_STOP;
				}
				break;

			//Look for stop bit
			case STATE_STOP:
				if(b != 1)
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(end);
					cap->m_samples.push_back(SDCmdSymbol(SDCmdSymbol::TYPE_ERROR, b));
				}

				pack->m_len = end - pack->m_offset;
				m_packets.push_back(pack);
				pack = NULL;

				state = STATE_IDLE;
				break;

			default:
				break;
		}
	}

	if(pack)
		delete pack;
}

bool SDCmdDecoder::GetShowDataColumn()
{
	return false;
}

Gdk::Color SDCmdDecoder::GetColor(int i)
{
	auto capture = dynamic_cast<SDCmdWaveform*>(GetData(0));
	if(capture != NULL)
	{
		auto s = capture->m_samples[i];
		switch(s.m_stype)
		{
			case SDCmdSymbol::TYPE_HEADER:
				return m_standardColors[COLOR_ADDRESS];

			case SDCmdSymbol::TYPE_COMMAND:
				return m_standardColors[COLOR_CONTROL];

			case SDCmdSymbol::TYPE_COMMAND_ARGS:
			case SDCmdSymbol::TYPE_RESPONSE_ARGS:
				return m_standardColors[COLOR_DATA];

			case SDCmdSymbol::TYPE_CRC_OK:
				return m_standardColors[COLOR_CHECKSUM_OK];

			case SDCmdSymbol::TYPE_CRC_BAD:
				return m_standardColors[COLOR_CHECKSUM_BAD];

			case SDCmdSymbol::TYPE_ERROR:
			default:
				return m_standardColors[COLOR_ERROR];
		}
	}
	return m_standardColors[COLOR_ERROR];
}

string SDCmdDecoder::GetText(int i)
{
	char tmp[128];

	CardType cardtype = static_cast<CardType>(m_parameters[m_cardtypename].GetIntVal());

	auto capture = dynamic_cast<SDCmdWaveform*>(GetData(0));
	if(capture != NULL)
	{
		auto s = capture->m_samples[i];
		switch(s.m_stype)
		{
			case SDCmdSymbol::TYPE_HEADER:
				if(s.m_data)
					return "CMD";
				else
					return "REPLY";

			case SDCmdSymbol::TYPE_COMMAND:
				{
					//We code ACMDs at offset 100
					string ret;
					if(s.m_data == 155)
						ret = string("CMD55");
					else if(s.m_data >= 100)
						ret = string("ACMD") + to_string(s.m_data - 100);
					else
						ret = string("CMD") + to_string(s.m_data);

					switch(s.m_data)
					{
						case 0:		return "GO_IDLE_STATE";
						//CMD1 reserved
						case 2:		return "ALL_SEND_CID";
						case 3:		return "SEND_RELATIVE_ADDR";
						case 4:		return "SET_DSR";

						//CMD5 reserved for SDIO.
						//eMMC uses it for SLEEP/AWAKE mode
						case 5:
							if(cardtype == SD_EMMC)
								return "SLEEP_AWAKE";
							break;

						case 6:
							if(cardtype == SD_EMMC)
								return "SWITCH";
							else
								return "SET_BUS_WIDTH";

						case 7:		return "SELECT_DESELECT_CARD";

						case 8:
							if(cardtype == SD_EMMC)
								return "SEND_EXT_CSD";
							else
								return "SEND_IF_COND";

						case 9:		return "SEND_CSD";
						case 10:	return "SEND_CID";
						case 11:	return "VOLTAGE_SWITCH";
						case 12:	return "STOP_TRANSMISSION";
						case 13:	return "SEND_STATUS";

						//CMD14 reserved
						//eMMC uses it for bus testing
						case 14:
							if(cardtype == SD_EMMC)
								return "BUSTEST_R";
							break;

						case 15:	return "GO_INACTIVE_STATE";
						case 16:	return "SET_BLOCKLEN";
						case 17:	return "READ_SINGLE_BLOCK";
						case 18:	return "READ_MULTIPLE_BLOCK";
						case 19:	return "SEND_TUNING_BLOCK";
						case 20:	return "SPEED_CLASS_CONTROL";

						//CMD21 reserved
						//eMMC uses it for link training in HS200 mode
						case 21:
							if(cardtype == SD_EMMC)
								return "SEND_TUNING_BLOCK";
							break;

						case 22:	return "ADDRESS_EXTENSION";
						case 23:	return "SET_BLOCK_COUNT";
						case 24:	return "WRITE_BLOCK";
						case 25:	return "WRITE_MULTIPLE_BLOCK";
						//CMD26 reserved for manufacturer
						case 27:	return "PROGRAM_CSD";
						case 28:	return "SET_WRITE_PROT";
						case 29:	return "CLR_WRITE_PROT";
						case 30:	return "SEND_WRITE_PROT";
						//CMD31 reserved
						case 32:	return "ERASE_WR_BLK_START";
						case 33:	return "ERASE_WR_BLK_END";
						//CMD34-38 TODO
						case 38:	return "ERASE";
						//CMD39 reserved
						//CMD40 defined by "DPS Spec" whatever that is
						//CMD41 reserved
						case 42:	return "LOCK_UNLOCK";

						//CMD52-54 reserved for SDIO mode

						case 55:
						case 155:	//What is the "RCA"?
							return "APP_CMD";

						case 56:	return "GEN_CMD";
						//CMD60-63 reserved for manufacturer

						//Parse CMD63 response depending on what the previous command was
						case 63:
						{
							//Should be a command at i-4
							if(i < 4)
								return "ERROR";
							return GetText(i-4);
						}

						//ACMD1-5 reserved
						case 106:	return "SET_BUS_WIDTH";
						//ACMD7-12 reserved
						case 113:	return "SD_STATUS";
						//ACMD14-16 reserved for DPS Specification
						//ACMD17 reserved
						//ACMD18 reserved for SD Security
						//ACMD19-21 reserved
						case 122:	return "SEND_NUM_WR_BLOCKS";
						case 123:	return "SET_WR_BLK_ERASE_COUNT";
						//ACMD24 reserved
						//ACMD25-26 reserved for SD Security
						//ACMD27 "shall not use"
						//ACMD28 reserved for DPS Specification
						//ACMD29 reserved
						//ACMD30-35 reserved for Security Specification
						//ACMD36-37 reserved
						//ACMD38 reserved for SD Security
						//ACMD39-40 reserved
						case 141:	return "SEND_OP_COND";
						case 142:	return "SET_CLR_CARD_DETECT";
						//ACMD43-49 reserved for SD Security
						//ACMD50?
						case 151:	return "SEND_SCR";
						//ACMD52-54 reserved for SD Security
						//ACMD55 is just CMD55
						//ACMD56-59 reserved for SD Security

						default:
							return ret;
					}
				}
				break;

			case SDCmdSymbol::TYPE_COMMAND_ARGS:
				{
					//Look up the command
					//(TYPE_COMMAND_ARGS can never be the first sample in a waveform)
					auto type = capture->m_samples[i-1].m_data;

					snprintf(tmp, sizeof(tmp), "%08x", s.m_data);
					string ret = tmp;

					switch(type)
					{
						//No arguments
						case 0:
						case 2:
							return "";

						//CMD5 SLEEP/AWAKE
						case 5:
							if(cardtype == SD_EMMC)
							{
								if(s.m_data & 0x8000)
									snprintf(tmp, sizeof(tmp), "RCA=%04x SLEEP", s.m_data >> 16);
								else
									snprintf(tmp, sizeof(tmp), "RCA=%04x WAKE", s.m_data >> 16);
								ret = tmp;
							}
							break;

						//CMD7 Select/Deselect Card
						case 7:
							snprintf(tmp, sizeof(tmp), "RCA=%04x", s.m_data >> 16);
							ret = tmp;
							break;

						//For eMMC: SEND_EXT_CSD (no arguments)
						//For SD: CMD8 Send Interface Condition (4.3.13)
						case 8:
							if(cardtype == SD_EMMC)
								ret = "";
							else
							{
								snprintf(tmp, sizeof(tmp), "%02x", s.m_data & 0xff);
								ret = string("Check ") + tmp;

								if(s.m_data & 0x2000)
									ret += " 1V2? ";
								if(s.m_data & 0x1000)
									ret += " PCIe? ";

								int voltage = (s.m_data >> 8) & 0xf;
								if(voltage == 1)
									ret += " 3V3";
								else
									ret += " Vunknown";
							}
							break;

						//CMD16 Set Block Length
						case 16:
							snprintf(tmp, sizeof(tmp), "Block size = %d", s.m_data);
							ret = tmp;
							break;

						//CMD17 Read Single Block
						//CMD18 Read Multiple Block
						//CMD24 Write Block
						//CMD25 Write Multiple Block
						case 17:
						case 18:
						case 24:
						case 25:
							snprintf(tmp, sizeof(tmp), "LBA = %08x", s.m_data);
							ret = tmp;
							break;

						//ACMD6 SET_BUS_WIDTH
						case 106:
							if( (s.m_data & 3) == 0)
								ret = "x1";
							else if( (s.m_data & 3) == 2)
								ret = "x4";
							else
								ret = "Invalid bus width";
							break;

						//ACMD41 SD_SEND_OP_COND
						//30 HCS
						//28 XPC
						//24 S18R
						//23:0 VDD range
						case 141:
							{
								ret = "";
								if(s.m_data & 0x40000000)
									ret += "HCS ";
								if(s.m_data & 0x10000000)
									ret += "XPC ";

								//VDD voltage window
								uint32_t window = s.m_data & 0xffffff;
								if(window == 0)
									ret += "Voltage?";
								else
								{
									float vmax = 0;
									float vmin = 3.6;

									if(window & 0x00800000)
									{
										vmax = max(vmax, 3.6f);
										vmin = min(vmin, 3.5f);
									}
									if(window & 0x00400000)
									{
										vmax = max(vmax, 3.5f);
										vmin = min(vmin, 3.4f);
									}
									if(window & 0x00200000)
									{
										vmax = max(vmax, 3.4f);
										vmin = min(vmin, 3.3f);
									}
									if(window & 0x00100000)
									{
										vmax = max(vmax, 3.3f);
										vmin = min(vmin, 3.2f);
									}
									if(window & 0x00080000)
									{
										vmax = max(vmax, 3.2f);
										vmin = min(vmin, 3.1f);
									}
									if(window & 0x00040000)
									{
										vmax = max(vmax, 3.1f);
										vmin = min(vmin, 3.0f);
									}
									if(window & 0x00020000)
									{
										vmax = max(vmax, 3.0f);
										vmin = min(vmin, 2.9f);
									}
									if(window & 0x00010000)
									{
										vmax = max(vmax, 2.9f);
										vmin = min(vmin, 2.8f);
									}
									if(window & 0x00008000)
									{
										vmax = max(vmax, 2.8f);
										vmin = min(vmin, 2.7f);
									}

									snprintf(tmp, sizeof(tmp), "Vdd = %.1f - %.1f", vmin, vmax);
									ret += tmp;
								}
							}
							break;

						//ACMD42 SET_CLR_CARD_DETECT
						case 142:
							if(s.m_data & 1)
								ret = "CD/DAT3 pullup enable";
							else
								ret = "CD/DAT3 pullup disable";

						default:
							break;
					}

					return ret;
				}

			case SDCmdSymbol::TYPE_RESPONSE_ARGS:
				{
					//Look up the command
					//(TYPE_RESPONSE_ARGS can never be the first sample in a waveform)
					auto type = capture->m_samples[i-1].m_data;

					//Back up by 5 (previous command) and figure out what we got
					if(i >= 5)
					{
						type = capture->m_samples[i-5].m_data;
					}

					snprintf(tmp, sizeof(tmp), "%08x", s.m_data);
					string ret = tmp;

					switch(type)
					{
						//4.9.3 R2 (CID or CSD register)
						case 2:
							{
								snprintf(tmp, sizeof(tmp), "%08x %08x %08x %08x ",
									s.m_data, s.m_extdata1, s.m_extdata2, s.m_extdata3);
								ret = tmp;
							}
							break;

						//4.9.5 R6 (Published RCA response)
						case 3:
							{
								snprintf(tmp, sizeof(tmp), "RCA=%04x ", s.m_data >> 16);
								ret = tmp;

								//Low 16 bits have same meaning as 23, 22, 19, 12:0 from normal card status

								if(s.m_data & 0x00008000)
									ret += "COM_CRC_ERROR ";
								if(s.m_data & 0x00004000)
									ret += "ILLEGAL_COMMAND ";
								if(s.m_data & 0x00002000)
									ret += "ERROR ";
								if(s.m_data & 0x00000100)
									ret += "READY_FOR_DATA ";
								if(s.m_data & 0x00000040)
									ret += "FX_EVENT ";
								if(s.m_data & 0x00000020)
									ret += "APP_CMD ";
								if(s.m_data & 0x00000010)
									ret += "RESERVED_SDIO ";
								if(s.m_data & 0x00000008)
									ret += "AKE_SEQ_ERR ";
								if(s.m_data & 0x00060084)
									ret += "RESERVED ";

								//12:9 CURRENT_STATE
								uint32_t state = (s.m_data >> 9) & 0xf;
								switch(state)
								{
									case 0:
										ret += "idle";
										break;

									case 1:
										ret += "ready";
										break;

									case 2:
										ret += "ident";
										break;

									case 3:
										ret += "stby";
										break;

									case 4:
										ret += "tran";
										break;

									case 5:
										ret += "data";
										break;

									case 6:
										ret += "rcv";
										break;

									case 7:
										ret += "prg";
										break;

									case 8:
										ret += "dis";
										break;

									default:
										ret += "reserved_state";
								}
							}
							break;


						//R7 Card Interface Condition (4.9.6)
						case 8:
							if(cardtype == SD_EMMC)
								ret = "";
							else
							{
								snprintf(tmp, sizeof(tmp), "%02x", s.m_data & 0xff);
								ret = string("Check ") + tmp;

								if(s.m_data & 0x2000)
									ret += " 1V2 ";
								if(s.m_data & 0x1000)
									ret += " PCIe ";

								int voltage = (s.m_data >> 8) & 0xf;
								if(voltage == 1)
									ret += " 3V3";
								else
									ret += " Vunknown";

							}
							break;

						//R3 OCR Register (4.9.4, 5.1). Operation Conditions Register Register :)
						case 141:
							{
								ret = "";
								if( (s.m_data & 0x80000000) == 0)
									ret += "BUSY ";

								//CCS bit is only valid after powerup is complete
								else
								{
									if(s.m_data & 0x08000000)
										ret += "UC";
									else if(s.m_data & 0x40000000)
										ret += "HC/XC ";
									else
										ret += "SC ";
								}

								if(s.m_data & 0x01000000)
									ret += "S18A ";

								//VDD voltage window
								uint32_t window = s.m_data & 0xffffff;
								if(window == 0)
									ret += "Voltage?";
								else
								{
									float vmax = 0;
									float vmin = 3.6;

									if(window & 0x00800000)
									{
										vmax = max(vmax, 3.6f);
										vmin = min(vmin, 3.5f);
									}
									if(window & 0x00400000)
									{
										vmax = max(vmax, 3.5f);
										vmin = min(vmin, 3.4f);
									}
									if(window & 0x00200000)
									{
										vmax = max(vmax, 3.4f);
										vmin = min(vmin, 3.3f);
									}
									if(window & 0x00100000)
									{
										vmax = max(vmax, 3.3f);
										vmin = min(vmin, 3.2f);
									}
									if(window & 0x00080000)
									{
										vmax = max(vmax, 3.2f);
										vmin = min(vmin, 3.1f);
									}
									if(window & 0x00040000)
									{
										vmax = max(vmax, 3.1f);
										vmin = min(vmin, 3.0f);
									}
									if(window & 0x00020000)
									{
										vmax = max(vmax, 3.0f);
										vmin = min(vmin, 2.9f);
									}
									if(window & 0x00010000)
									{
										vmax = max(vmax, 2.9f);
										vmin = min(vmin, 2.8f);
									}
									if(window & 0x00008000)
									{
										vmax = max(vmax, 2.8f);
										vmin = min(vmin, 2.7f);
									}

									snprintf(tmp, sizeof(tmp), "Vdd = %.1f - %.1f", vmin, vmax);
									ret += tmp;
								}
							}
							break;

						//Parse anything else as R1 Card Status (4.10)
						default:
							{
								ret = "";
								if(s.m_data & 0x80000000)
									ret += "OUT_OF_RANGE ";
								if(s.m_data & 0x40000000)
									ret += "ADDRESS_ERROR ";
								if(s.m_data & 0x20000000)
									ret += "BLOCK_LEN_ERROR ";
								if(s.m_data & 0x10000000)
									ret += "ERASE_SEQ_ERROR ";
								if(s.m_data & 0x08000000)
									ret += "ERASE_PARAM ";
								if(s.m_data & 0x04000000)
									ret += "WP_VIOLATION ";
								if(s.m_data & 0x02000000)
									ret += "CARD_IS_LOCKED ";
								if(s.m_data & 0x01000000)
									ret += "LOCK_UNLOCK_FAILED ";
								if(s.m_data & 0x00800000)
									ret += "COM_CRC_ERROR ";
								if(s.m_data & 0x00400000)
									ret += "ILLEGAL_COMMAND ";
								if(s.m_data & 0x00200000)
									ret += "CARD_ECC_FAILED ";
								if(s.m_data & 0x00100000)
									ret += "CC_ERROR ";
								if(s.m_data & 0x00080000)
									ret += "ERROR ";
								if(s.m_data & 0x00010000)
									ret += "CSD_OVERWRITE ";
								if(s.m_data & 0x00008000)
									ret += "WP_ERASE_SKIP ";
								if(s.m_data & 0x00004000)
									ret += "CARD_ECC_DISABLED ";
								if(s.m_data & 0x00002000)
									ret += "ERASE_RESET ";
								if(s.m_data & 0x00000100)
									ret += "READY_FOR_DATA ";
								if(s.m_data & 0x00000040)
									ret += "FX_EVENT ";
								if(s.m_data & 0x00000020)
									ret += "APP_CMD ";
								if(s.m_data & 0x00000010)
									ret += "RESERVED_SDIO ";
								if(s.m_data & 0x00000008)
									ret += "AKE_SEQ_ERR ";
								if(s.m_data & 0x00060084)
									ret += "RESERVED ";

								//12:9 CURRENT_STATE
								uint32_t state = (s.m_data >> 9) & 0xf;
								switch(state)
								{
									case 0:
										ret += "idle";
										break;

									case 1:
										ret += "ready";
										break;

									case 2:
										ret += "ident";
										break;

									case 3:
										ret += "stby";
										break;

									case 4:
										ret += "tran";
										break;

									case 5:
										ret += "data";
										break;

									case 6:
										ret += "rcv";
										break;

									case 7:
										ret += "prg";
										break;

									case 8:
										ret += "dis";
										break;

									default:
										ret += "reserved_state";
								}
							}
							break;
					}

					return ret;
				}

			case SDCmdSymbol::TYPE_CRC_OK:
			case SDCmdSymbol::TYPE_CRC_BAD:
				snprintf(tmp, sizeof(tmp), "CRC: %02x", s.m_data);
				return string(tmp);

			case SDCmdSymbol::TYPE_ERROR:
			default:
				return "ERROR";
		}
	}
	return "";
}

vector<string> SDCmdDecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("Type");
	ret.push_back("Code");
	ret.push_back("Command");
	ret.push_back("Info");
	return ret;
}


bool SDCmdDecoder::CanMerge(Packet* /*first*/, Packet* cur, Packet* next)
{
	//Merge reply with the preceding command
	if( (cur->m_headers["Type"] == "Command") && (next->m_headers["Type"] == "Reply") )
		return true;

	//If the previous is a CMD55 reply, we can merge the ACMD request with it
	if( (cur->m_headers["Type"] == "Reply") && (cur->m_headers["Code"] == "CMD55") &&
		(next->m_headers["Type"] == "Command") )
	{
		return true;
	}

	//If the previous is an ACMD41 reply, and this is an ACMD request, merge the powerup polling
	//FIXME: this will falsely merge other ACMDs after!!
	if( (cur->m_headers["Type"] == "Reply") && (cur->m_headers["Code"] == "ACMD41") &&
		(next->m_headers["Type"] == "Command") && (next->m_headers["Code"] == "CMD55") )
	{
		return true;
	}

	return false;
}

Packet* SDCmdDecoder::CreateMergedHeader(Packet* pack, size_t i)
{
	if(pack->m_headers["Type"] == "Command")
	{
		Packet* ret = new Packet;
		ret->m_offset = pack->m_offset;
		ret->m_len = pack->m_len;

		//Default to copying everything
		ret->m_headers["Type"] = "Command";
		ret->m_headers["Code"] = pack->m_headers["Code"];
		ret->m_headers["Command"] = pack->m_headers["Command"];
		ret->m_displayBackgroundColor = pack->m_displayBackgroundColor;
		ret->m_headers["Info"] = pack->m_headers["Info"];

		//If the header is a CMD55 packet, check the actual ACMD and use that instead
		if( (pack->m_headers["Code"] == "CMD55") && (i+2 < m_packets.size()) )
		{
			Packet* next = m_packets[i+2];

			ret->m_headers["Command"] = next->m_headers["Command"];
			ret->m_headers["Code"] = next->m_headers["Code"];
			ret->m_displayBackgroundColor = next->m_displayBackgroundColor;
			ret->m_headers["Info"] = next->m_headers["Info"];

			//Summarize ACMD41 with reply data
			if(next->m_headers["Code"] == "ACMD41")
			{
				//Keep on looking at replies until we see the final ACMD41
				size_t last = i+2;
				for(size_t j=i; j<m_packets.size(); j++)
				{
					if(m_packets[j]->m_headers["Type"] != "Reply")
						continue;
					else if(m_packets[j]->m_headers["Code"] == "CMD55")
						continue;
					else if(m_packets[j]->m_headers["Code"] == "ACMD41")
						last = j;
					else
						break;
				}
				ret->m_headers["Info"] += string(", got ") + m_packets[last]->m_headers["Info"];
			}
		}

		//Summarize CMD2 and CMD3 with reply data
		if( (pack->m_headers["Code"] == "CMD2") && (i+1 < m_packets.size()) )
			ret->m_headers["Info"] = m_packets[i+1]->m_headers["Info"];
		if( (pack->m_headers["Code"] == "CMD3") && (i+1 < m_packets.size()) )
			ret->m_headers["Info"] = m_packets[i+1]->m_headers["Info"];

		return ret;
	}

	return NULL;
}
