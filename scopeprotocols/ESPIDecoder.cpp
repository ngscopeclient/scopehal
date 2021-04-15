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
	CreateInput("spi_mosi");
	CreateInput("spi_miso");
	CreateInput("qspi");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool ESPIDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 3) && (dynamic_cast<SPIWaveform*>(stream.m_channel->GetData(0)) != NULL) )
		return true;

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
	snprintf(hwname, sizeof(hwname), "eSPI(%s)",	GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

vector<string> ESPIDecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("Command");
	ret.push_back("Address");
/*	ret.push_back("Info");
	ret.push_back("Len");*/
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

	auto din = dynamic_cast<SPIWaveform*>(GetInputWaveform(0));
	auto dout = dynamic_cast<SPIWaveform*>(GetInputWaveform(1));
	auto dquad = dynamic_cast<SPIWaveform*>(GetInputWaveform(2));
	if(!din || !dout || !dquad)
	{
		SetData(NULL, 0);
		return;
	}

	size_t quadlen = dquad->m_samples.size();

	//Create the waveform. Call SetData() early on so we can use GetText() in the packet decode
	auto cap = new ESPIWaveform;
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	SetData(cap, 0);

	//Loop over the SPI events and process stuff
	//For now, assume the MISO/MOSI SPI captures are synchronized (sample N is at the same point in time)
	ESPISymbol samp;
	size_t len = din->m_samples.size();
	enum
	{
		STATE_IDLE,
		STATE_OPCODE,
		STATE_CONFIG_ADDRESS,

		STATE_HANG
	} state = STATE_IDLE;

	ESPISymbol::ESpiCommand current_cmd = ESPISymbol::COMMAND_RESET;
	Packet* pack = NULL;
	bool quad_mode = false;
	size_t count = 0;
	uint64_t addr = 0;
	size_t tstart = 0;
	for(size_t iin = 0, iquad=0; (iin < len) && (iquad < quadlen) ;)
	{
		//Figure out what the incoming packet is.
		auto s = din->m_samples[iin];
		auto r = dout->m_samples[iin];
		auto sq = dquad->m_samples[iquad];

		switch(state)
		{
			//When idle, look for a TYPE_SELECT frame.
			//This re-synchronizes us to the start of a new transaction
			case STATE_IDLE:
				if(s.m_stype == SPISymbol::TYPE_SELECT)
				{
					state = STATE_OPCODE;

					//Discard samples on the quad bus until we're synchronized
					while(iquad < quadlen)
					{
						if(dquad->m_offsets[iquad]*dquad->m_timescale >= din->m_offsets[iin]*din->m_timescale)
							break;
						iquad ++;
					}
					sq = dquad->m_samples[iquad];

					//Expect a select symbol
					if(sq.m_stype == SPISymbol::TYPE_SELECT)
						iquad ++;
					else
						LogWarning("Expected select on quad bus, got something else\n");
				}
				iin ++;
				break;	//end STATE_IDLE

			//Frame should begin with an opcode
			case STATE_OPCODE:
				if(s.m_stype != SPISymbol::TYPE_DATA)
				{
					cap->m_offsets.push_back(din->m_offsets[iin]);
					cap->m_durations.push_back(din->m_durations[iin]);
					cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_ERROR));

					iin ++;
				}
				else
				{
					pack = new Packet;

					//HEURISTIC
					//If the MISO byte is not 0xFF, we're probably in quad mode
					if(r.m_data != 0xff)
					{
						current_cmd = (ESPISymbol::ESpiCommand)sq.m_data;

						//Add symbol for packet type
						tstart = dquad->m_offsets[iquad];
						pack->m_offset = tstart * dquad->m_timescale;
						cap->m_offsets.push_back(tstart);
						cap->m_durations.push_back(dquad->m_durations[iquad]);
						cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_COMMAND_TYPE, sq.m_data));

						quad_mode = true;
						iquad ++;
						state = STATE_IDLE;
					}
					else
					{
						current_cmd = (ESPISymbol::ESpiCommand)s.m_data;

						//Add symbol for packet type
						tstart = din->m_offsets[iin];
						pack->m_offset = tstart * din->m_timescale;
						cap->m_offsets.push_back(tstart);
						cap->m_durations.push_back(din->m_durations[iin]);
						cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_COMMAND_TYPE, s.m_data));

						quad_mode = false;
						iin ++;
					}

					//Create a new packet for this transaction
					pack->m_len = 0;
					m_packets.push_back(pack);
					pack->m_headers["Command"] = GetText(cap->m_samples.size()-1);

					//Decide what to do based on the opcode
					count = 0;
					addr = 0;
					switch(current_cmd)
					{
						//Expect a 16 bit address
						case ESPISymbol::COMMAND_GET_CONFIGURATION:
						case ESPISymbol::COMMAND_SET_CONFIGURATION:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_CONTROL];
							state = STATE_CONFIG_ADDRESS;
							break;

						//TODO
						case ESPISymbol::COMMAND_PUT_IORD_SHORT_x1:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							state = STATE_IDLE;
							break;
						case ESPISymbol::COMMAND_PUT_IORD_SHORT_x2:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							state = STATE_IDLE;
							break;
						case ESPISymbol::COMMAND_PUT_IORD_SHORT_x4:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							state = STATE_IDLE;
							break;

						//TODO
						case ESPISymbol::COMMAND_PUT_IOWR_SHORT_x1:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
							state = STATE_IDLE;
							break;
						case ESPISymbol::COMMAND_PUT_IOWR_SHORT_x2:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
							state = STATE_IDLE;
							break;
						case ESPISymbol::COMMAND_PUT_IOWR_SHORT_x4:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
							state = STATE_IDLE;
							break;

						//TODO
						case ESPISymbol::COMMAND_PUT_PC:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
							state = STATE_IDLE;
							break;
						case ESPISymbol::COMMAND_GET_PC:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							state = STATE_IDLE;
							break;

						//TODO
						case ESPISymbol::COMMAND_GET_VWIRE:
						case ESPISymbol::COMMAND_PUT_VWIRE:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_CONTROL];
							state = STATE_IDLE;
							break;

						//TODO
						case ESPISymbol::COMMAND_GET_STATUS:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_STATUS];
							state = STATE_IDLE;
							break;

						//TODO
						case ESPISymbol::COMMAND_RESET:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_COMMAND];
							state = STATE_IDLE;
							break;

						case ESPISymbol::COMMAND_GET_FLASH_NP:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							state = STATE_IDLE;
							break;
						case ESPISymbol::COMMAND_PUT_FLASH_C:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
							state = STATE_IDLE;
							break;

						case ESPISymbol::COMMAND_GET_OOB:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							state = STATE_IDLE;
							break;
						case ESPISymbol::COMMAND_PUT_OOB:
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
							state = STATE_IDLE;
							break;


						//Unknown
						default:
							state = STATE_IDLE;
							break;
					}
				}
				break;	//end STATE_OPCODE

			case STATE_CONFIG_ADDRESS:
				if(quad_mode)
				{
					LogWarning("STATE_CONFIG_ADDRESS doesn't handle quad mode yet\n");
					state = STATE_HANG;
					break;
				}

				if(s.m_stype != SPISymbol::TYPE_DATA)
				{
					cap->m_offsets.push_back(din->m_offsets[iin]);
					cap->m_durations.push_back(din->m_durations[iin]);
					cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_ERROR));

					iin ++;
				}
				else
				{
					//Save start time
					if(count == 0)
					{
						tstart = din->m_offsets[iin];
						cap->m_offsets.push_back(tstart);
					}

					//Save data
					addr = (addr << 8) | s.m_data;
					count ++;

					//Add data
					if(count == 2)
					{
						cap->m_durations.push_back(din->m_offsets[iin] + din->m_durations[iin] - tstart);
						cap->m_samples.push_back(ESPISymbol(ESPISymbol::TYPE_COMMAND_ADDR_16, addr));

						//TODO
						state = STATE_IDLE;
					}

					iin ++;
				}
				break;	//end STATE_CONFIG_ADDRESS

			//When in doubt, move to the next full SPI sample
			default:
				iin ++;
		}
	}
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
				return m_standardColors[COLOR_CONTROL];

			case ESPISymbol::TYPE_COMMAND_ADDR_16:
			case ESPISymbol::TYPE_COMMAND_ADDR_32:
			case ESPISymbol::TYPE_COMMAND_ADDR_64:
				return m_standardColors[COLOR_ADDRESS];

			//	return m_standardColors[COLOR_DATA];

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

			case ESPISymbol::TYPE_COMMAND_ADDR_16:
				snprintf(tmp, sizeof(tmp), "Addr: %04lx", s.m_data);
				return tmp;

			case ESPISymbol::TYPE_COMMAND_ADDR_32:
				snprintf(tmp, sizeof(tmp), "Addr: %08lx", s.m_data);
				return tmp;

			case ESPISymbol::TYPE_COMMAND_ADDR_64:
				snprintf(tmp, sizeof(tmp), "Addr: %016lx", s.m_data);
				return tmp;

			case ESPISymbol::TYPE_ERROR:
			default:
				return "ERROR";
		}

		return string(tmp);
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
