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

SDCmdDecoder::SDCmdDecoder(string color)
	: PacketDecoder(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_MEMORY)
{
	CreateInput("CMD");
	CreateInput("CLK");
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

	//Reference: SD Physical Layer Simplified Specification v.8.00

	int last_cmd = 0;
	bool app_cmd = false;
	for(size_t i=0; i<len; i++)
	{
		bool b = dcmd.m_samples[i];
		int64_t off = dcmd.m_offsets[i];
		int64_t dur = dcmd.m_durations[i];
		int64_t end = off + dur;

		switch(state)
		{
			//Wait for a start bit
			case STATE_IDLE:
				if(!b)
				{
					tstart = off;
					state = STATE_TYPE;
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
					state = STATE_COMMAND_HEADER;
				else
					state = STATE_RESPONSE_HEADER;

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

					cap->m_samples.push_back(SDCmdSymbol(SDCmdSymbol::TYPE_COMMAND, data));

					//Save the command code so we know how to parse replies
					last_cmd = data;

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
				}
				break;

			//Read arguments for a response packet
			case STATE_RESPONSE_BODY:
				{
					//Figure out the expected reply format (4.7.4)
					int reply_len = 0;
					switch(last_cmd)
					{
						//32-bit reply for all other commands
						default:
							reply_len = 32;
							break;
					}

					data = (data << 1) | b;
					nbit ++;
					if(nbit == reply_len)
					{
						cap->m_offsets.push_back(tstart);
						cap->m_durations.push_back(end - tstart);
						cap->m_samples.push_back(SDCmdSymbol(SDCmdSymbol::TYPE_RESPONSE_ARGS, data));

						data = 0;
						nbit = 0;
						tstart = end;
						state = STATE_CRC;
					}

				}
				break;

			//Reads the CRC
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

				state = STATE_IDLE;
				break;

			default:
				break;
		}
	}

	SetData(cap, 0);
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
	char tmp[32];

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
						case 0:
							return ret + " GO_IDLE_STATE";

						case 8:
							return ret + " SEND_IF_COND";

						case 55:
							return ret + " APP_CMD";

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
						//CMD8 Send Interface Condition (4.3.13)
						case 8:
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


						//31:12 reserved
						//11:8 = supply voltage
						//7:0 = check pattern
							break;

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

					snprintf(tmp, sizeof(tmp), "%08x", s.m_data);
					string ret = tmp;

					switch(type)
					{
						//R7 Card Interface Condition (4.9.6)
						case 8:
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
	ret.push_back("Op");
	ret.push_back("Address");
	ret.push_back("Info");
	ret.push_back("Len");
	return ret;
}
