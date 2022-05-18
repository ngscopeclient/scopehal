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
#include "SPIFlashDecoder.h"
#include "SPIDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SPIFlashDecoder::SPIFlashDecoder(const string& color)
	: PacketDecoder(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_MEMORY)
{
	CreateInput("spi_in");
	CreateInput("spi_out");
	CreateInput("qspi");

	m_typename = "Flash Type";
	m_parameters[m_typename] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_typename].AddEnumValue("Generic (3-byte address)", FLASH_TYPE_GENERIC_3BYTE_ADDRESS);
	m_parameters[m_typename].AddEnumValue("Generic (4-byte address)", FLASH_TYPE_GENERIC_4BYTE_ADDRESS);
	m_parameters[m_typename].AddEnumValue("Winbond W25N", FLASH_TYPE_WINBOND_W25N);
	m_parameters[m_typename].SetIntVal(FLASH_TYPE_GENERIC_3BYTE_ADDRESS);

	m_outfile = "Dump File";
	m_parameters[m_outfile] = FilterParameter(FilterParameter::TYPE_FILENAME, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_outfile].m_fileFilterMask = "*.bin";
	m_parameters[m_outfile].m_fileFilterName = "Binary files (*.bin)";
	m_parameters[m_outfile].m_fileIsOutput = true;

	m_fpOut = NULL;
}

SPIFlashDecoder::~SPIFlashDecoder()
{
	if(m_fpOut)
	{
		fclose(m_fpOut);
		m_fpOut = NULL;
	}
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

bool SPIFlashDecoder::NeedsConfig()
{
	return true;
}

vector<string> SPIFlashDecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("Op");
	ret.push_back("Address");
	ret.push_back("Info");
	ret.push_back("Len");
	return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void SPIFlashDecoder::Refresh()
{
	ClearPackets();

	FlashType flashtype = (FlashType)m_parameters[m_typename].GetIntVal();

	//Open dump file, if applicable
	auto fname = m_parameters[m_outfile].GetFileName();
	if(m_cachedfname != fname)
	{
		m_cachedfname = fname;
		if(m_fpOut)
			fclose(m_fpOut);
		m_fpOut = fopen(fname.c_str(), "w+b");
	}

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

	//Create the waveform. Call SetData() early on so we can use GetText() in the packet decode
	auto cap = new SPIFlashWaveform;
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	cap->m_triggerPhase = din->m_triggerPhase;
	SetData(cap, 0);

	//Number of address bytes used (for generic flash only, not W25N)
	int num_address_bytes = 3;
	if(flashtype == FLASH_TYPE_GENERIC_4BYTE_ADDRESS)
		num_address_bytes = 4;

	//Loop over the SPI events and process stuff
	//For now, assume the MISO/MOSI SPI captures are synchronized (sample N is at the same point in time)
	SPIFlashSymbol samp;
	size_t len = din->m_samples.size();
	enum
	{
		STATE_IDLE,
		STATE_OPCODE,
		STATE_QUAD_ADDRESS,
		STATE_ADDRESS,
		STATE_READ_DATA,
		STATE_QUAD_DATA,
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
	Packet* pack = NULL;
	for(size_t iin = 0; iin+1 < len; iin ++)
	{
		//Figure out what the incoming packet is
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
					//Create the packet
					pack = new Packet;
					pack->m_offset = din->m_offsets[iin] * din->m_timescale + din->m_triggerPhase;
					pack->m_len = 0;
					m_packets.push_back(pack);

					//Parse the command
					switch(s.m_data)
					{
						////////////////////////////////////////////////////////////////////////////////////////////////
						// JEDEC standard commands (in numerical order)

						//Write the status register
						case 0x01:
						case 0x1f:
							current_cmd = SPIFlashSymbol::CMD_WRITE_STATUS_REGISTER;

							//We need an address here
							if(flashtype == FLASH_TYPE_WINBOND_W25N)
							{
								state = STATE_ADDRESS;
								address_bytes_left = 1;
								addr = 0;
								addr_start = din->m_offsets[iin+1];
							}

							//No address for standard flash
							else
								state = STATE_WRITE_DATA;

							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_CONTROL];
							break;

						//x1 program
						case 0x02:
							current_cmd = SPIFlashSymbol::CMD_PAGE_PROGRAM;
							state = STATE_ADDRESS;
							addr = 0;
							addr_start = din->m_offsets[iin+1];

							switch(flashtype)
							{
								case FLASH_TYPE_WINBOND_W25N:
									address_bytes_left = 2;
									break;

								case FLASH_TYPE_GENERIC_3BYTE_ADDRESS:
								case FLASH_TYPE_GENERIC_4BYTE_ADDRESS:
								default:
									address_bytes_left = num_address_bytes;
									break;
							}

							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_COMMAND];
							break;

						//Slow read (no dummy clocks)
						case 0x03:
							current_cmd = SPIFlashSymbol::CMD_READ;
							state = STATE_ADDRESS;
							addr = 0;
							addr_start = din->m_offsets[iin+1];

							switch(flashtype)
							{
								case FLASH_TYPE_WINBOND_W25N:
									address_bytes_left = 2;
									break;

								case FLASH_TYPE_GENERIC_3BYTE_ADDRESS:
								case FLASH_TYPE_GENERIC_4BYTE_ADDRESS:
								default:
									address_bytes_left = num_address_bytes;
									break;
							}

							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							break;

						//Clear write enable flag
						case 0x04:
							current_cmd = SPIFlashSymbol::CMD_WRITE_DISABLE;
							state = STATE_IDLE;

							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_CONTROL];
							break;

						//Read status register 1
						case 0x05:
							current_cmd = SPIFlashSymbol::CMD_READ_STATUS_REGISTER_1;
							state = STATE_READ_DATA;
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_STATUS];
							break;

						//Set write enable flag
						case 0x06:
							current_cmd = SPIFlashSymbol::CMD_WRITE_ENABLE;
							state = STATE_IDLE;

							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_CONTROL];
							break;

						//Fast read (with dummy clocks)
						case 0x0b:
							current_cmd = SPIFlashSymbol::CMD_FAST_READ;

							addr = 0;
							addr_start = din->m_offsets[iin+1];
							state = STATE_ADDRESS;

							switch(flashtype)
							{
								case FLASH_TYPE_WINBOND_W25N:
									address_bytes_left = 2;
									break;

								case FLASH_TYPE_GENERIC_3BYTE_ADDRESS:
								case FLASH_TYPE_GENERIC_4BYTE_ADDRESS:
								default:
									address_bytes_left = num_address_bytes;
									break;
							}

							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							break;

						//Read the status register
						case 0x0f:
						case 0x5f:
							current_cmd = SPIFlashSymbol::CMD_READ_STATUS_REGISTER;

							//We need an address here
							if(flashtype == FLASH_TYPE_WINBOND_W25N)
							{
								state = STATE_ADDRESS;
								address_bytes_left = 1;
								addr = 0;
								addr_start = din->m_offsets[iin+1];
							}

							//No address for standard flash
							else
								state = STATE_READ_DATA;

							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_STATUS];
							break;

						case 0x13:

							//Read a page of NAND
							if(flashtype == FLASH_TYPE_WINBOND_W25N)
							{
								current_cmd = SPIFlashSymbol::CMD_W25N_READ_PAGE;
								state = STATE_DUMMY_BEFORE_ADDRESS;
								address_bytes_left = 2;
								addr = 0;

								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_COMMAND];
							}

							//Normal Winbond flashes use this as read data like 0x03, but with a 32-bit address
							else
							{
								current_cmd = SPIFlashSymbol::CMD_READ;
								state = STATE_ADDRESS;
								addr = 0;
								addr_start = din->m_offsets[iin+1];
								address_bytes_left = 4;
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							}

							break;

						//Read status register 3
						case 0x15:
							current_cmd = SPIFlashSymbol::CMD_READ_STATUS_REGISTER_3;
							state = STATE_READ_DATA;
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_STATUS];
							break;

						//Quad input page program
						case 0x32:
							current_cmd = SPIFlashSymbol::CMD_QUAD_PAGE_PROGRAM;
							state = STATE_ADDRESS;
							addr = 0;
							addr_start = din->m_offsets[iin+1];

							switch(flashtype)
							{
								case FLASH_TYPE_WINBOND_W25N:
									address_bytes_left = 2;
									break;

								case FLASH_TYPE_GENERIC_3BYTE_ADDRESS:
								case FLASH_TYPE_GENERIC_4BYTE_ADDRESS:
								default:
									address_bytes_left = num_address_bytes;
									break;
							}

							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
							break;

						//0x3b 1-1-2 fast read

						//Read status register 2
						case 0x35:
							current_cmd = SPIFlashSymbol::CMD_READ_STATUS_REGISTER_2;
							state = STATE_READ_DATA;
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_STATUS];
							break;

						//Read SFDP
						case 0x5a:
							current_cmd = SPIFlashSymbol::CMD_READ_SFDP;
							state = STATE_ADDRESS;
							addr = 0;
							addr_start = din->m_offsets[iin+1];
							address_bytes_left = 3;
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_CONTROL];
							break;

						//1-1-4 fast read
						case 0x6b:
							current_cmd = SPIFlashSymbol::CMD_READ_1_1_4;
							state = STATE_ADDRESS;
							addr = 0;
							addr_start = din->m_offsets[iin+1];

							switch(flashtype)
							{
								case FLASH_TYPE_WINBOND_W25N:
									address_bytes_left = 2;
									break;

								case FLASH_TYPE_GENERIC_3BYTE_ADDRESS:
								case FLASH_TYPE_GENERIC_4BYTE_ADDRESS:
								default:
									address_bytes_left = num_address_bytes;
									break;
							}

							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							break;

						case 0x66:
							current_cmd = SPIFlashSymbol::CMD_ENABLE_RESET;
							state = STATE_IDLE;
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_COMMAND];
							break;

						//1-1-4 fast read with 32-bit address regardless of mode register
						case 0x6c:
							current_cmd = SPIFlashSymbol::CMD_READ_1_1_4;
							state = STATE_ADDRESS;
							addr = 0;
							addr_start = din->m_offsets[iin+1];
							address_bytes_left = 4;
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							break;

						//Read the IDCODE
						//Winbond W25N has a dummy cycle before the reply, standard flash does not
						case 0x9f:
							current_cmd = SPIFlashSymbol::CMD_READ_JEDEC_ID;

							switch(flashtype)
							{
								case FLASH_TYPE_WINBOND_W25N:
									state = STATE_DUMMY_BEFORE_DATA;
									break;

								case FLASH_TYPE_GENERIC_3BYTE_ADDRESS:
								case FLASH_TYPE_GENERIC_4BYTE_ADDRESS:
								default:
									state = STATE_READ_DATA;
									break;
							}

							data_type = SPIFlashSymbol::TYPE_VENDOR_ID;
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_STATUS];
							break;

						//Release from power down
						//TODO: can also be used to read device ID
						case 0xab:
							current_cmd = SPIFlashSymbol::CMD_RELEASE_PD;
							state = STATE_IDLE;
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_COMMAND];
							break;

						//0xbb 1-2-2 fast read

						//Enter 4-byte address mode
						case 0xb7:
							current_cmd = SPIFlashSymbol::CMD_ADDR_32BIT;
							state = STATE_IDLE;
							num_address_bytes = 4;
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_COMMAND];
							break;

						//Erase a block (size is device dependent)
						//Commonly 64 or 128 Kbytes
						case 0xd8:
							current_cmd = SPIFlashSymbol::CMD_BLOCK_ERASE;
							state = STATE_ADDRESS;
							addr = 0;
							addr_start = din->m_offsets[iin+1];

							switch(flashtype)
							{
								case FLASH_TYPE_WINBOND_W25N:
									address_bytes_left = 2;
									break;

								case FLASH_TYPE_GENERIC_3BYTE_ADDRESS:
								case FLASH_TYPE_GENERIC_4BYTE_ADDRESS:
								default:
									address_bytes_left = num_address_bytes;
									break;
							}

							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_COMMAND];
							break;

						//Enter 3-byte address mode
						case 0xe9:
							current_cmd = SPIFlashSymbol::CMD_ADDR_24BIT;
							state = STATE_IDLE;
							num_address_bytes = 3;
							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_COMMAND];
							break;

						//1-4-4 fast read
						case 0xeb:
							current_cmd = SPIFlashSymbol::CMD_READ_1_4_4;
							state = STATE_QUAD_ADDRESS;
							addr = 0;
							addr_start = din->m_offsets[iin+1];

							switch(flashtype)
							{
								case FLASH_TYPE_WINBOND_W25N:
									address_bytes_left = 2;
									break;

								case FLASH_TYPE_GENERIC_3BYTE_ADDRESS:
								case FLASH_TYPE_GENERIC_4BYTE_ADDRESS:
								default:
									address_bytes_left = num_address_bytes;
									break;
							}

							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							break;

						//Reset should occur by itself, ignore any data after that
						case 0x99:
						case 0xff:
							current_cmd = SPIFlashSymbol::CMD_RESET;
							state = STATE_IDLE;

							//Most flashes w/ 32 bit addressing revert to 24 bit on a reset.
							//TODO: only some models do this? or depends on nonvolatile SFR?
							num_address_bytes = 3;

							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_COMMAND];
							break;

						////////////////////////////////////////////////////////////////////////////////////////////////
						// Winbond W25N specific below here

						//Execute the program operation
						case 0x10:
							current_cmd = SPIFlashSymbol::CMD_W25N_PROGRAM_EXECUTE;
							state = STATE_DUMMY_BEFORE_ADDRESS;
							address_bytes_left = 2;
							addr = 0;

							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_COMMAND];
							break;

						//0x0c fast read with 4 byte address
						//0x34 quad random load program data
						//0x3c fast read dual with 4 byte address
						//0x6c fast read quad with 4 byte address
						//0x84 random load program data
						//0xA1 write bad block table
						//0xA5 read bad block table
						//0xA9 read last ECC failure address
						//0xBC fast read dual i/o with 4 byte address
						//0xEC fast read quad i/o with 4 byte address

						//Drop unknown commands
						default:
							current_cmd = SPIFlashSymbol::CMD_UNKNOWN;
							state = STATE_IDLE;

							pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
							break;
					}

					//Generate a sample for it
					cap->m_offsets.push_back(din->m_offsets[iin]);
					cap->m_durations.push_back(din->m_durations[iin]);
					cap->m_samples.push_back(SPIFlashSymbol(SPIFlashSymbol::TYPE_COMMAND, current_cmd, 0));

					pack->m_headers["Op"] = GetText(cap->m_samples.size() - 1);
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
							state = STATE_QUAD_DATA;
							break;
						default:
							state = STATE_READ_DATA;
							break;
					}
				}

				break;

			//Read address in QSPI mode
			case STATE_QUAD_ADDRESS:

				if(!dquad)
				{
					LogWarning("Encountered a quad SPI command, but no quad data provided\n");
					state = STATE_IDLE;
					break;
				}

				//Discard quad samples until we're lined up with the start of the x1 sample
				while(iquad < quadlen)
				{
					if(dquad->m_offsets[iquad] >= din->m_offsets[iin])
						break;
					iquad ++;
				}

				//Read quad samples until we finish the address
				while( (iquad < quadlen) && (address_bytes_left > 0) )
				{
					auto squad = dquad->m_samples[iquad];
					if(squad.m_stype != SPISymbol::TYPE_DATA)
						break;

					//Save the address byte
					addr = (addr << 8) | squad.m_data;
					address_bytes_left --;

					iquad ++;
				}
				if(m_fpOut)
					fseek(m_fpOut, addr, SEEK_SET);

				//Add the address
				cap->m_offsets.push_back(addr_start);
				cap->m_durations.push_back(dquad->m_offsets[iquad-1] + dquad->m_durations[iquad-1] - addr_start);
				cap->m_samples.push_back(SPIFlashSymbol(
					SPIFlashSymbol::TYPE_ADDRESS, SPIFlashSymbol::CMD_UNKNOWN, addr));

				{
					char tmp[128] = "";
					snprintf(tmp, sizeof(tmp), "%x", addr);
					pack->m_headers["Address"] = tmp;
				}

				//Dummy clocks before read data
				switch(flashtype)
				{
					case FLASH_TYPE_GENERIC_3BYTE_ADDRESS:
					case FLASH_TYPE_GENERIC_4BYTE_ADDRESS:

						//TODO: implement continuous read mode
						//For now, just throw away the mode bits and call it a wait state
						cap->m_offsets.push_back(dquad->m_offsets[iquad]);
						cap->m_durations.push_back(dquad->m_durations[iquad]);
						cap->m_samples.push_back(SPIFlashSymbol(
							SPIFlashSymbol::TYPE_DUMMY, SPIFlashSymbol::CMD_UNKNOWN, 0));
						iquad ++;

						//fall through

					default:
						for(int j=0; j<2; j++)
						{
							cap->m_offsets.push_back(dquad->m_offsets[iquad]);
							cap->m_durations.push_back(dquad->m_durations[iquad]);
							cap->m_samples.push_back(SPIFlashSymbol(
								SPIFlashSymbol::TYPE_DUMMY, SPIFlashSymbol::CMD_UNKNOWN, 0));
							iquad ++;
						}
						break;
				}

				state = STATE_QUAD_DATA;
				data_type = SPIFlashSymbol::TYPE_DATA;

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
						if(m_fpOut)
							fseek(m_fpOut, addr, SEEK_SET);

						//Default setup
						data_type = SPIFlashSymbol::TYPE_DATA;
						addr_type = SPIFlashSymbol::TYPE_ADDRESS;
						state = STATE_READ_DATA;

						switch(current_cmd)
						{
							case SPIFlashSymbol::CMD_READ:

								//W25N is weird and needs dummy clocks even with the slow 0x03 read instruction
								if(flashtype == FLASH_TYPE_WINBOND_W25N)
									state = STATE_DUMMY_BEFORE_DATA;
								else
									state = STATE_READ_DATA;

								break;

							case SPIFlashSymbol::CMD_FAST_READ:
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

							//Writing a page
							case SPIFlashSymbol::CMD_PAGE_PROGRAM:
								state = STATE_WRITE_DATA;
								break;

							//Writing a page in x4 mode
							case SPIFlashSymbol::CMD_QUAD_PAGE_PROGRAM:
								state = STATE_QUAD_DATA;
								break;

							//Reading SFDP
							case SPIFlashSymbol::CMD_READ_SFDP:
								state = STATE_DUMMY_BEFORE_DATA;

							default:
								break;
						}

						cap->m_offsets.push_back(addr_start);
						cap->m_durations.push_back(din->m_offsets[iin] + din->m_durations[iin] - addr_start);
						cap->m_samples.push_back(SPIFlashSymbol(
							addr_type, SPIFlashSymbol::CMD_UNKNOWN, addr));

						if(addr_type == SPIFlashSymbol::TYPE_ADDRESS)
						{
							char tmp[128] = "";
							snprintf(tmp, sizeof(tmp), "%x", addr);
							pack->m_headers["Address"] = tmp;
						}
						else
							pack->m_headers["Address"] = GetText(cap->m_samples.size() - 1);
					}
				}

				break;

			//Read data
			case STATE_READ_DATA:
				if(s.m_stype != SPISymbol::TYPE_DATA)
				{
					//At the end of a read command, crack status registers if needed
					if(data_type != SPIFlashSymbol::TYPE_DATA)
					{
						//If ID code, crack both
						if(data_type == SPIFlashSymbol::TYPE_PART_ID)
						{
							pack->m_headers["Info"] +=
								GetText(cap->m_samples.size()-2) +
								" " +
								GetText(cap->m_samples.size()-1);
						}
						else
							pack->m_headers["Info"] = GetText(cap->m_samples.size()-1);
					}

					//Only write to the output for actual flash data!
					//We don't want to save descriptors.
					if(current_cmd != SPIFlashSymbol::CMD_READ_SFDP)
					{
						if(m_fpOut)
							fwrite(&pack->m_data[0], 1, pack->m_data.size(), m_fpOut);
					}

					state = STATE_IDLE;
				}
				else
				{
					//See what the last sample we produced was
					//If it was a part ID, just append to it
					size_t pos = cap->m_samples.size() - 1;
					if( (data_type == SPIFlashSymbol::TYPE_PART_ID) &
						(cap->m_samples[pos].m_type == SPIFlashSymbol::TYPE_PART_ID) )
					{
						cap->m_samples[pos].m_data = (cap->m_samples[pos].m_data << 8) | dout->m_samples[iin].m_data;
					}

					//Normal data
					else
					{
						cap->m_offsets.push_back(dout->m_offsets[iin]);
						cap->m_durations.push_back(dout->m_durations[iin]);
						cap->m_samples.push_back(SPIFlashSymbol(
							data_type, SPIFlashSymbol::CMD_UNKNOWN, dout->m_samples[iin].m_data));
					}

					//Extend the packet
					pack->m_data.push_back(dout->m_samples[iin].m_data);
					pack->m_len = (dout->m_offsets[iin] + dout->m_durations[iin])*dout->m_timescale +
						dout->m_triggerPhase - pack->m_offset;
					char tmp[128];
					snprintf(tmp, sizeof(tmp), "%zu", pack->m_data.size());
					pack->m_headers["Len"] = tmp;

					//If reading multibyte special value (vendor ID etc), handle that
					switch(data_type)
					{
						case SPIFlashSymbol::TYPE_VENDOR_ID:
							data_type = SPIFlashSymbol::TYPE_PART_ID;
							break;

						default:
							break;
					}
				}

				break;	//STATE_READ_DATA

			//Read or write data in quad mode
			case STATE_QUAD_DATA:

				if(!dquad)
				{
					LogWarning("Encountered a quad SPI command, but no quad data provided\n");
					state = STATE_IDLE;
					break;
				}

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

					//Extend the packet
					pack->m_data.push_back(dquad->m_samples[iquad].m_data);
					pack->m_len = (dquad->m_offsets[iquad] + dquad->m_durations[iquad])*dquad->m_timescale +
						dquad->m_triggerPhase - pack->m_offset;
					char tmp[128];
					snprintf(tmp, sizeof(tmp), "%zu", pack->m_data.size());
					pack->m_headers["Len"] = tmp;

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

				if(m_fpOut)
					fwrite(&pack->m_data[0], 1, pack->m_data.size(), m_fpOut);

				state = STATE_IDLE;
				break;

			//Write data
			case STATE_WRITE_DATA:
				if(s.m_stype != SPISymbol::TYPE_DATA)
				{
					state = STATE_IDLE;

					//At the end of a write command, crack status registers if needed
					if(data_type != SPIFlashSymbol::TYPE_DATA)
						pack->m_headers["Info"] = GetText(cap->m_samples.size()-1);
				}
				else
				{
					cap->m_offsets.push_back(din->m_offsets[iin]);
					cap->m_durations.push_back(din->m_durations[iin]);
					cap->m_samples.push_back(SPIFlashSymbol(
						data_type, SPIFlashSymbol::CMD_UNKNOWN, din->m_samples[iin].m_data));

					//Extend the packet
					pack->m_data.push_back(din->m_samples[iin].m_data);
					pack->m_len = (din->m_offsets[iin] + din->m_durations[iin]) * din->m_timescale +
						din->m_triggerPhase - pack->m_offset;
					char tmp[128];
					snprintf(tmp, sizeof(tmp), "%zu", pack->m_data.size());
					pack->m_headers["Len"] = tmp;
				}
				break;
		}
	}
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
			case SPIFlashSymbol::TYPE_VENDOR_ID:
			case SPIFlashSymbol::TYPE_PART_ID:
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

			case SPIFlashSymbol::TYPE_VENDOR_ID:
				switch(s.m_data)
				{
					case VENDOR_ID_CYPRESS:
						return "Cypress";
					case VENDOR_ID_MICRON:
						return "Micron";
					case VENDOR_ID_WINBOND:
						return "Winbond";

					default:
						snprintf(tmp, sizeof(tmp), "0x%x", s.m_data);
						break;
				}
				break;

			//Part ID depends on vendor ID
			case SPIFlashSymbol::TYPE_PART_ID:
				return GetPartID(capture, s, i);

			case SPIFlashSymbol::TYPE_COMMAND:
				switch(s.m_cmd)
				{
					case SPIFlashSymbol::CMD_READ:
						return "Read";
					case SPIFlashSymbol::CMD_READ_SFDP:
						return "Read SFDP";
					case SPIFlashSymbol::CMD_FAST_READ:
						return "Read Fast";
					case SPIFlashSymbol::CMD_READ_1_1_4:
						return "Read Quad";
					case SPIFlashSymbol::CMD_READ_1_4_4:
						return "Read Quad I/O";
					case SPIFlashSymbol::CMD_READ_JEDEC_ID:
						return "Read JEDEC ID";
					case SPIFlashSymbol::CMD_READ_STATUS_REGISTER:
						return "Read Status";
					case SPIFlashSymbol::CMD_READ_STATUS_REGISTER_1:
						return "Read Status Register 1";
					case SPIFlashSymbol::CMD_READ_STATUS_REGISTER_2:
						return "Read Status Register 2";
					case SPIFlashSymbol::CMD_READ_STATUS_REGISTER_3:
						return "Read Status Register 3";
					case SPIFlashSymbol::CMD_WRITE_STATUS_REGISTER:
						return "Write Status";
					case SPIFlashSymbol::CMD_RESET:
						return "Reset";
					case SPIFlashSymbol::CMD_WRITE_DISABLE:
						return "Write Disable";
					case SPIFlashSymbol::CMD_WRITE_ENABLE:
						return "Write Enable";
					case SPIFlashSymbol::CMD_BLOCK_ERASE:
						return "Block Erase";
					case SPIFlashSymbol::CMD_PAGE_PROGRAM:
						return "Page Program";
					case SPIFlashSymbol::CMD_ADDR_24BIT:
						return "Select 24-Bit Address";
					case SPIFlashSymbol::CMD_ADDR_32BIT:
						return "Select 32-Bit Address";
					case SPIFlashSymbol::CMD_RELEASE_PD:
						return "Release from Power Down";
					case SPIFlashSymbol::CMD_ENABLE_RESET:
						return "Enable Reset";

					//W25N specific
					case SPIFlashSymbol::CMD_W25N_PROGRAM_EXECUTE:
						return "Program Execute";
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

string SPIFlashDecoder::GetPartID(SPIFlashWaveform* cap, const SPIFlashSymbol& s, int i)
{
	char tmp[128];

	//Look up the vendor ID
	auto ivendor = cap->m_samples[i-1];
	switch(ivendor.m_data)
	{
		case VENDOR_ID_CYPRESS:
			switch(s.m_data)
			{
				//QSPI NOR
				case 0x0217:
					return "S25FS064x";

				default:
					snprintf(tmp, sizeof(tmp), "%x", s.m_data);
					return tmp;
			}
			break;

		case VENDOR_ID_MICRON:
			switch(s.m_data)
			{
				//(Q)SPI NOR
				case 0x2014:
					return "M25P80";
				case 0x2018:
					return "M25P128";
				case 0x7114:
					return "M25PX80";
				case 0x8014:
					return "M25PE80";
				case 0xba19:
					return "N25Q256x";
				case 0xbb18:
					return "N25Q128x";

				default:
					snprintf(tmp, sizeof(tmp), "%x", s.m_data);
					return tmp;
			}
			break;

		case VENDOR_ID_WINBOND:
			switch(s.m_data)
			{
				//QSPI NOR
				case 0x4014:
					return "W25Q80xx";
				case 0x4018:
					return "W25Q128xx";
				case 0x6015:
					return "W25Q16xx";
				case 0x6016:
					return "W25Q32xx";
				case 0x6018:
					return "W25Q128xx (QPI mode)";
				case 0x7018:
					return "W25Q128JV-DTR";

				//QSPI NAND
				case 0xaa21:
					return "W25N01GV";

				default:
					snprintf(tmp, sizeof(tmp), "%x", s.m_data);
					return tmp;
			}
			break;
	}

	//Unknown vendor, print part number as hex
	snprintf(tmp, sizeof(tmp), "%x", s.m_data);
	return tmp;
}

bool SPIFlashDecoder::CanMerge(Packet* first, Packet* /*cur*/, Packet* next)
{
	//Merge read-status packets
	if( (first->m_headers["Op"] == "Read Status") && (next->m_headers["Op"] == "Read Status") )
		return true;

	return false;
}

Packet* SPIFlashDecoder::CreateMergedHeader(Packet* pack, size_t /*i*/)
{
	if(pack->m_headers["Op"] == "Read Status")
	{
		Packet* ret = new Packet;
		ret->m_offset = pack->m_offset;
		ret->m_len = pack->m_len;			//TODO: extend?
		ret->m_headers["Op"] = "Poll Status";
		ret->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_STATUS];

		//TODO: add other fields?
		return ret;
	}

	return NULL;
}
