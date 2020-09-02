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
#include "SPIFlashDecoder.h"
#include "SPIDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SPIFlashDecoder::SPIFlashDecoder(string color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_MEMORY)
{
	CreateInput("spi_in");
	CreateInput("spi_out");
	CreateInput("qspi");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool SPIFlashDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	//Allow null for the QSPI input, since some flashes run in x1 mode
	if((i == 2) && (stream.m_channel == NULL) )
		return true;

	if(stream.m_channel == NULL)
		return false;

	if( (i < 3) && (dynamic_cast<SPIWaveform*>(stream.m_channel->GetData(0)) != NULL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string SPIFlashDecoder::GetProtocolName()
{
	return "SPI Flash";
}

bool SPIFlashDecoder::IsOverlay()
{
	return true;
}

bool SPIFlashDecoder::NeedsConfig()
{
	return true;
}

void SPIFlashDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "SPIFlash(%s)",	GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void SPIFlashDecoder::Refresh()
{
	//Input/output x1 inputs are required
	if( (m_inputs[0].m_channel == NULL) || (m_inputs[1].m_channel == NULL) )
	{
		SetData(NULL, 0);
		return;
	}
	auto din = dynamic_cast<SPIWaveform*>(GetInputWaveform(0));
	auto dout = dynamic_cast<SPIWaveform*>(GetInputWaveform(1));
	SPIWaveform* dquad = NULL;
	if(m_inputs[2].m_channel != NULL)
		dquad = dynamic_cast<SPIWaveform*>(GetInputWaveform(2));
	if(!din || !dout)
	{
		SetData(NULL, 0);
		return;
	}

	size_t iquad = 0;
	size_t quadlen = 0;
	if(dquad)
		quadlen = dquad->m_samples.size();

	//Loop over the SPI events and process stuff
	//For now, assume the MISO/MOSI SPI captures are synchronized (sample N is at the same point in time)
	auto cap = new SPIFlashWaveform;
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startPicoseconds = din->m_startPicoseconds;
	SPIFlashSymbol samp;
	size_t len = din->m_samples.size();
	enum
	{
		STATE_IDLE,
		STATE_OPCODE,
		STATE_QUAD_ADDRESS,
		STATE_ADDRESS,
		STATE_READ_DATA,
		STATE_READ_QUAD_DATA,
		STATE_WRITE_DATA,
		STATE_DUMMY_BEFORE_ADDRESS,
		STATE_DUMMY_BEFORE_DATA
	} state = STATE_IDLE;

	SPIFlashSymbol::FlashCommand current_cmd = SPIFlashSymbol::CMD_UNKNOWN;
	int address_bytes_left = 0;
	uint32_t addr = 0;
	int64_t addr_start;
	SPIFlashSymbol::FlashType data_type = SPIFlashSymbol::TYPE_DATA;
	SPIFlashSymbol::FlashType addr_type = SPIFlashSymbol::TYPE_ADDRESS;
	for(size_t iin = 0; iin+1 < len; iin ++)
	{
		//Figure out what the incoming command is
		auto s = din->m_samples[iin];

		switch(state)
		{
			//When idle, look for a TYPE_SELECT frame.
			//This re-synchronizes us to the start of a new transaction
			case STATE_IDLE:
				if(s.m_stype == SPISymbol::TYPE_SELECT)
					state = STATE_OPCODE;
				break;

			//Read the opcode. Should be a TYPE_DATA frame
			case STATE_OPCODE:
				if(s.m_stype != SPISymbol::TYPE_DATA)
					state = STATE_IDLE;
				else
				{
					//Parse the command
					switch(s.m_data)
					{
						//Reset should occur by itself, ignore any data after that
						case 0xff:
							current_cmd = SPIFlashSymbol::CMD_RESET;
							state = STATE_IDLE;
							break;

						//Read the IDCODE
						case 0x9f:
							current_cmd = SPIFlashSymbol::CMD_READ_JEDEC_ID;
							state = STATE_READ_DATA;
							break;

						//Read the status register
						//TODO: this is W25N specific, normal NOR flash jumps right into the data
						//(need enum parameters!!!)
						case 0x0f:
						case 0x5f:
							current_cmd = SPIFlashSymbol::CMD_READ_STATUS_REGISTER;
							state = STATE_ADDRESS;
							address_bytes_left = 1;
							addr = 0;
							addr_start = din->m_offsets[iin+1];
							break;

						//Write the status register
						//TODO: W25N specific
						case 0x01:
						case 0x1f:
							current_cmd = SPIFlashSymbol::CMD_WRITE_STATUS_REGISTER;
							state = STATE_ADDRESS;
							address_bytes_left = 1;
							addr = 0;
							addr_start = din->m_offsets[iin+1];
							break;

						//1-1-4 fast read
						case 0x6b:
							current_cmd = SPIFlashSymbol::CMD_READ_1_1_4;
							state = STATE_ADDRESS;
							address_bytes_left = 2;		//TODO: this is device specific, current value is for W25N
							addr = 0;
							addr_start = din->m_offsets[iin+1];
							break;

						//1-4-4 fast read
						case 0xeb:
							current_cmd = SPIFlashSymbol::CMD_READ_1_4_4;
							state = STATE_QUAD_ADDRESS;
							address_bytes_left = 2;		//TODO: this is device specific, current value is for W25N
							addr = 0;
							addr_start = din->m_offsets[iin+1];
							break;

						//Slow read (no dummy clocks)
						case 0x03:
							current_cmd = SPIFlashSymbol::CMD_READ;
							state = STATE_ADDRESS;
							address_bytes_left = 2;		//TODO: this is device specific, current value is for W25N
							addr = 0;
							addr_start = din->m_offsets[iin+1];
							break;

						////////////////////////////////////////////////////////////////////////////////////////////////
						// Winbond W25N specific below here

						//Read a page of NAND
						case 0x13:
							current_cmd = SPIFlashSymbol::CMD_W25N_READ_PAGE;
							state = STATE_DUMMY_BEFORE_ADDRESS;
							address_bytes_left = 2;
							addr = 0;
							break;

						//Drop unknown commands
						default:
							current_cmd = SPIFlashSymbol::CMD_UNKNOWN;
							state = STATE_IDLE;
							break;
					}

					//Generate a sample for it
					cap->m_offsets.push_back(din->m_offsets[iin]);
					cap->m_durations.push_back(din->m_durations[iin]);
					cap->m_samples.push_back(SPIFlashSymbol(SPIFlashSymbol::TYPE_COMMAND, current_cmd, 0));
				}
				break;

			//Dummy bytes before address (ignore)
			case STATE_DUMMY_BEFORE_ADDRESS:

				cap->m_offsets.push_back(din->m_offsets[iin]);
				cap->m_durations.push_back(din->m_durations[iin]);
				cap->m_samples.push_back(SPIFlashSymbol(
					SPIFlashSymbol::TYPE_DUMMY, SPIFlashSymbol::CMD_UNKNOWN, 0));

				//Address starts on the next sample
				addr_start = din->m_offsets[iin+1];

				if(s.m_stype != SPISymbol::TYPE_DATA)
					state = STATE_IDLE;
				else
					state = STATE_ADDRESS;

				break;

			//Dummy bytes before data (ignore)
			case STATE_DUMMY_BEFORE_DATA:

				cap->m_offsets.push_back(din->m_offsets[iin]);
				cap->m_durations.push_back(din->m_durations[iin]);
				cap->m_samples.push_back(SPIFlashSymbol(
					SPIFlashSymbol::TYPE_DUMMY, SPIFlashSymbol::CMD_UNKNOWN, 0));

				if(s.m_stype != SPISymbol::TYPE_DATA)
					state = STATE_IDLE;
				else
				{
					//Figure out what type of data we're dealing with
					switch(current_cmd)
					{
						case SPIFlashSymbol::CMD_READ_1_1_4:
						case SPIFlashSymbol::CMD_READ_1_4_4:
							state = STATE_READ_QUAD_DATA;
							break;
						default:
							state = STATE_READ_DATA;
							break;
					}
				}

				break;

			//Read address in QSPI mode
			case STATE_QUAD_ADDRESS:
				//TODO
				if(s.m_stype != SPISymbol::TYPE_DATA)
					state = STATE_IDLE;
				break;

			//Read address in SPI mode
			case STATE_ADDRESS:
				if(s.m_stype != SPISymbol::TYPE_DATA)
					state = STATE_IDLE;
				else
				{
					//Save the address byte
					addr = (addr << 8) | s.m_data;
					address_bytes_left --;

					//If this is the last address byte, generate a block sample for the whole thing
					if(address_bytes_left == 0)
					{
						//Default setup
						data_type = SPIFlashSymbol::TYPE_DATA;
						addr_type = SPIFlashSymbol::TYPE_ADDRESS;
						state = STATE_READ_DATA;

						switch(current_cmd)
						{
							case SPIFlashSymbol::CMD_READ:
								//W25N is weird and needs dummy clocks even with the slow 0x03 read instruction
								state = STATE_DUMMY_BEFORE_DATA;
								break;

							//If we're accessing the status register, check the address
							//TODO: W25N specific
							case SPIFlashSymbol::CMD_READ_STATUS_REGISTER:
							case SPIFlashSymbol::CMD_WRITE_STATUS_REGISTER:
								if( (addr & 0xf0) == 0xa0)
									data_type = SPIFlashSymbol::TYPE_W25N_SR_PROT;
								else if( (addr & 0xf0) == 0xb0)
									data_type = SPIFlashSymbol::TYPE_W25N_SR_CONFIG;
								else if( (addr & 0xf0) == 0xc0)
									data_type = SPIFlashSymbol::TYPE_W25N_SR_STATUS;

								//Writing, not reading
								if(current_cmd == SPIFlashSymbol::CMD_WRITE_STATUS_REGISTER)
									state = STATE_WRITE_DATA;

								//Decode this as a status register address
								addr_type = SPIFlashSymbol::TYPE_W25N_SR_ADDR;
								break;

							//If we're reading a page, decode as a block address
							case SPIFlashSymbol::CMD_W25N_READ_PAGE:
								addr_type = SPIFlashSymbol::TYPE_W25N_BLOCK_ADDR;
								break;

							//Fast read has dummy clocks before data
							case SPIFlashSymbol::CMD_READ_1_1_4:
								state = STATE_DUMMY_BEFORE_DATA;
								break;

							default:
								break;
						}

						cap->m_offsets.push_back(addr_start);
						cap->m_durations.push_back(din->m_offsets[iin] + din->m_durations[iin] - addr_start);
						cap->m_samples.push_back(SPIFlashSymbol(
							addr_type, SPIFlashSymbol::CMD_UNKNOWN, addr));
					}
				}

				break;

			//Read data
			case STATE_READ_DATA:
				if(s.m_stype != SPISymbol::TYPE_DATA)
					state = STATE_IDLE;
				else
				{
					cap->m_offsets.push_back(dout->m_offsets[iin]);
					cap->m_durations.push_back(dout->m_durations[iin]);
					cap->m_samples.push_back(SPIFlashSymbol(
						data_type, SPIFlashSymbol::CMD_UNKNOWN, dout->m_samples[iin].m_data));
				}
				break;

			//Read data in quad mode
			case STATE_READ_QUAD_DATA:

				//Discard quad samples until we're lined up with the start of the x1 sample
				while(iquad < quadlen)
				{
					if(dquad->m_offsets[iquad] >= din->m_offsets[iin])
						break;
					iquad ++;
				}

				//Read quad samples until we get to a deselect event
				while(iquad < quadlen)
				{
					auto squad = dquad->m_samples[iquad];
					if(squad.m_stype != SPISymbol::TYPE_DATA)
						break;

					//Copy the data
					cap->m_offsets.push_back(dquad->m_offsets[iquad]);
					cap->m_durations.push_back(dquad->m_durations[iquad]);
					cap->m_samples.push_back(SPIFlashSymbol(
						data_type, SPIFlashSymbol::CMD_UNKNOWN, dquad->m_samples[iquad].m_data));

					iquad ++;
				}

				//Realign the x1 sample stream to where we left off
				while(iin+1 < len)
				{
					if(din->m_offsets[iin] >= dquad->m_offsets[iquad])
						break;

					iin ++;
				}
				iin --;

				state = STATE_IDLE;
				break;

			//Write data
			case STATE_WRITE_DATA:
				if(s.m_stype != SPISymbol::TYPE_DATA)
					state = STATE_IDLE;
				else
				{
					cap->m_offsets.push_back(din->m_offsets[iin]);
					cap->m_durations.push_back(din->m_durations[iin]);
					cap->m_samples.push_back(SPIFlashSymbol(
						data_type, SPIFlashSymbol::CMD_UNKNOWN, din->m_samples[iin].m_data));
				}
				break;
		}
	}

	SetData(cap, 0);
}

Gdk::Color SPIFlashDecoder::GetColor(int i)
{
	auto capture = dynamic_cast<SPIFlashWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const SPIFlashSymbol& s = capture->m_samples[i];

		switch(s.m_type)
		{
			case SPIFlashSymbol::TYPE_DUMMY:
				return m_standardColors[COLOR_IDLE];

			case SPIFlashSymbol::TYPE_COMMAND:
				return m_standardColors[COLOR_CONTROL];

			case SPIFlashSymbol::TYPE_ADDRESS:
			case SPIFlashSymbol::TYPE_W25N_SR_ADDR:
			case SPIFlashSymbol::TYPE_W25N_BLOCK_ADDR:
				return m_standardColors[COLOR_ADDRESS];

			case SPIFlashSymbol::TYPE_DATA:
			case SPIFlashSymbol::TYPE_W25N_SR_CONFIG:
			case SPIFlashSymbol::TYPE_W25N_SR_PROT:
			case SPIFlashSymbol::TYPE_W25N_SR_STATUS:
				return m_standardColors[COLOR_DATA];

			default:
				return m_standardColors[COLOR_ERROR];
		}
	}

	return m_standardColors[COLOR_ERROR];
}

string SPIFlashDecoder::GetText(int i)
{
	auto capture = dynamic_cast<SPIFlashWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const SPIFlashSymbol& s = capture->m_samples[i];
		char tmp[128];

		switch(s.m_type)
		{
			case SPIFlashSymbol::TYPE_DUMMY:
				return "Wait state";

			case SPIFlashSymbol::TYPE_COMMAND:
				switch(s.m_cmd)
				{
					case SPIFlashSymbol::CMD_READ:
						return "Read";
					case SPIFlashSymbol::CMD_READ_1_1_4:
						return "Quad Read";
					case SPIFlashSymbol::CMD_READ_1_4_4:
						return "Quad I/O Read";
					case SPIFlashSymbol::CMD_READ_JEDEC_ID:
						return "Read JEDEC ID";
					case SPIFlashSymbol::CMD_READ_STATUS_REGISTER:
						return "Read Status";
					case SPIFlashSymbol::CMD_WRITE_STATUS_REGISTER:
						return "Write Status";
					case SPIFlashSymbol::CMD_RESET:
						return "Reset";
					case SPIFlashSymbol::CMD_W25N_READ_PAGE:
						return "Read Page";

					default:
						return "Unknown Cmd";
				}

			case SPIFlashSymbol::TYPE_ADDRESS:
				snprintf(tmp, sizeof(tmp), "Addr 0x%x", s.m_data);
				break;

			case SPIFlashSymbol::TYPE_DATA:
				snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
				break;

			////////////////////////////////////////////////////////////////////////////////////////////////////////////
			// Winbond W25N specific

			case SPIFlashSymbol::TYPE_W25N_BLOCK_ADDR:
				snprintf(tmp, sizeof(tmp), "Block %x", s.m_data);
				break;

			//Address of a W25N status register
			case SPIFlashSymbol::TYPE_W25N_SR_ADDR:
				if( (s.m_data & 0xf0) == 0xa0)
					return "Protection";
				else if( (s.m_data & 0xf0) == 0xb0)
					return "Config";
				else if( (s.m_data & 0xf0) == 0xc0)
					return "Status";
				else
					snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
				break;

			//W25N status registers
			case SPIFlashSymbol::TYPE_W25N_SR_CONFIG:
				{
					string ret = "";
					if(s.m_data & 0x80)
						ret += "OTP-LOCK ";
					if(s.m_data & 0x40)
						ret += "OTP-WR ";
					if(s.m_data & 0x20)
						ret += "SR1-LOCK ";
					if(s.m_data & 0x10)
						ret += "ECCEN ";
					if(s.m_data & 0x08)
						ret += "BUFFER ";
					else
						ret += "CONTINUOUS ";
					return ret;
				}
				break;	//TYPE_W25N_SR_CONFIG

			case SPIFlashSymbol::TYPE_W25N_SR_PROT:
				return "TODO prot";
				break;

			case SPIFlashSymbol::TYPE_W25N_SR_STATUS:
				{
					string ret = "";
					if(s.m_data & 0x40)
						ret += "LUT-F ";

					uint8_t eccstat = (s.m_data >> 3) & 3;
					switch(eccstat)
					{
						case 0:
							ret += "ECC-OK ";
							break;

						case 1:
							ret += "ECC-CORR ";
							break;

						case 2:
							ret += "ECC-UNCORR-SINGLE ";
							break;

						case 3:
							ret += "ECC-UNCORR-MULTI ";
							break;
					}

					if(s.m_data & 8)
						ret += "PROG-FAIL ";

					if(s.m_data & 4)
						ret += "ERASE-FAIL ";

					if(s.m_data & 2)
						ret += "WRITABLE ";

					if(s.m_data & 1)
						ret += "BUSY";

					return ret;
				}
				break;	//TYPE_W25N_SR_STATUS

			default:
				return "ERROR";
		}

		return string(tmp);
	}
	return "";
}
