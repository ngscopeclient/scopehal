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
#include "I2CEepromDecoder.h"
#include "I2CDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

I2CEepromDecoder::I2CEepromDecoder(const string& color)
	: PacketDecoder(color, CAT_MEMORY)
{
	CreateInput("i2c");

	m_memtypename = "Address Bits";
	m_parameters[m_memtypename] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_memtypename].AddEnumValue("4 (24C00)", 4);
	m_parameters[m_memtypename].AddEnumValue("7 (24C01)", 7);
	m_parameters[m_memtypename].AddEnumValue("8 (24C02)", 8);
	m_parameters[m_memtypename].AddEnumValue("9 (24C04)", 9);
	m_parameters[m_memtypename].AddEnumValue("10 (24C08)", 10);
	m_parameters[m_memtypename].AddEnumValue("11 (24C16)", 11);
	m_parameters[m_memtypename].AddEnumValue("12 (24C32)", 12);
	m_parameters[m_memtypename].AddEnumValue("13 (24C64 / 24C65)", 13);
	//TODO: support block write protect and high endurance block in 24x65
	m_parameters[m_memtypename].AddEnumValue("14 (24C128)", 14);
	m_parameters[m_memtypename].AddEnumValue("15 (24C256)", 15);
	m_parameters[m_memtypename].AddEnumValue("16 (24C512)", 16);

	//These devices steal extra I2C address LSBs as memory addresses.
	//Maybe they're multiple stacked 24C512s?
	m_parameters[m_memtypename].AddEnumValue("16+1 (24CM01)", 17);
	m_parameters[m_memtypename].AddEnumValue("16+2 (24CM02)", 18);
	m_parameters[m_memtypename].SetIntVal(04);

	m_baseaddrname = "Base Address";
	m_parameters[m_baseaddrname] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_baseaddrname].AddEnumValue("0xA0 (standard 24C)", 0xa0);
	m_parameters[m_baseaddrname].AddEnumValue("0xB0 (AT24MAC address)", 0xb0);
	m_parameters[m_baseaddrname].SetIntVal(0xa0);

	m_addrpinname = "Address Pins";
	m_parameters[m_addrpinname] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_addrpinname].AddEnumValue("A[2:0] = 000", 0x0);
	m_parameters[m_addrpinname].AddEnumValue("A[2:0] = 001", 0x2);
	m_parameters[m_addrpinname].AddEnumValue("A[2:0] = 010", 0x4);
	m_parameters[m_addrpinname].AddEnumValue("A[2:0] = 011", 0x6);
	m_parameters[m_addrpinname].AddEnumValue("A[2:0] = 100", 0x8);
	m_parameters[m_addrpinname].AddEnumValue("A[2:0] = 101", 0xa);
	m_parameters[m_addrpinname].AddEnumValue("A[2:0] = 110", 0xc);
	m_parameters[m_addrpinname].AddEnumValue("A[2:0] = 111", 0xe);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool I2CEepromDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (dynamic_cast<I2CWaveform*>(stream.m_channel->GetData(0)) != NULL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

vector<string> I2CEepromDecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("Type");
	ret.push_back("Address");
	ret.push_back("Len");
	return ret;
}

string I2CEepromDecoder::GetProtocolName()
{
	return "I2C EEPROM";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void I2CEepromDecoder::Refresh()
{
	ClearPackets();

	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	auto din = dynamic_cast<I2CWaveform*>(GetInputWaveform(0));
	if(!din)
	{
		SetData(NULL, 0);
		return;
	}

	//Pull out our settings
	uint8_t base_addr = m_parameters[m_baseaddrname].GetIntVal() | m_parameters[m_addrpinname].GetIntVal();
	int raw_bits = m_parameters[m_memtypename].GetIntVal();
	int device_bits = 0;
	if(raw_bits > 16)
		device_bits = raw_bits - 16;
	int pointer_bits = min(16, raw_bits);

	//Set up output
	auto cap = new I2CEepromWaveform;
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;

	//Mask for device address
	uint8_t base_mask;
	switch(device_bits)
	{
		case 2:
			base_mask = 0xf8;
			break;

		case 1:
			base_mask = 0xfc;
			break;

		case 0:
		default:
			base_mask = 0xfe;
			break;
	}

	//Main decode loop
	I2CEepromSymbol samp;
	size_t len = din->m_samples.size();
	int state = 0;
	int64_t tstart = 0;
	uint32_t ptr = 0;
	int addr_count = 0;
	size_t ntype = 0;
	uint8_t last_device_addr = 0;
	Packet* pack = NULL;
	for(size_t i=0; i<len; i++)
	{
		auto s = din->m_samples[i];
		int64_t end = din->m_offsets[i] + din->m_durations[i];

		switch(state)
		{
			//Expect a start bit, ignore anything before that.
			//Restarts are OK too, if we're right after another transaction.
			case 0:
				if( (s.m_stype == I2CSymbol::TYPE_START) || (s.m_stype == I2CSymbol::TYPE_RESTART) )
				{
					tstart = din->m_offsets[i];
					state = 1;

					//Create a new packet. If we already have an incomplete one that got aborted, reset it
					if(pack)
					{
						pack->m_data.clear();
						pack->m_headers.clear();
					}
					else
						pack = new Packet;

					pack->m_offset = din->m_offsets[i] * din->m_timescale;
					pack->m_len = 0;
				}
				break;

			//Should be device address
			case 1:
				if(s.m_stype == I2CSymbol::TYPE_ADDRESS)
				{
					//If address bits don't match, discard it
					if( (s.m_data & base_mask) != base_addr)
					{
						state = 0;
						continue;
					}

					last_device_addr = s.m_data;

					//Process extra memory address bits in the device address, if needed (for 24CM series)
					switch(device_bits)
					{
						case 2:
							ptr = (s.m_data & 0x6) >> 1;
							break;

						case 1:
							ptr = (s.m_data & 0x2) >> 1;
							break;

						default:
						case 0:
							ptr = 0;
							break;
					}

					//We should always be an I2C write (setting address pointer) even if reading data
					//TODO: support reads continuing from the last address without updating the pointer
					if(s.m_data & 1)
						state = 0;

					//Expect ACK/NAK then move on
					else
					{
						//Offset left if device_bits is nonzero
						size_t ui = (din->m_durations[i]) / 8;
						end -= device_bits * ui;

						cap->m_offsets.push_back(tstart);
						cap->m_durations.push_back(end - tstart);
						cap->m_samples.push_back(I2CEepromSymbol(I2CEepromSymbol::TYPE_SELECT_READ, 0));
						state = 2;

						tstart = end;
					}
				}
				else
					state = 0;
				break;

			//Expect an ACK and extend the device address if no device bits.
			//If NAK, we're actually a TYPE_POLL_BUSY
			case 2:
				if(s.m_stype == I2CSymbol::TYPE_ACK)
				{
					//Extend the address sample as needed
					size_t nlast = cap->m_offsets.size() - 1;
					if(device_bits == 0)
					{
						cap->m_durations[nlast] += din->m_durations[i];
						tstart += din->m_durations[i];
					}

					//Move on to the memory address
					state = 3;
					addr_count = 0;
					ntype = nlast;

					//If NAK, don't look for more transaction data
					if(s.m_data)
					{
						cap->m_samples[nlast].m_type = I2CEepromSymbol::TYPE_POLL_BUSY;
						pack->m_headers["Type"] = "Poll - Busy";
						pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_STATUS];
						m_packets.push_back(pack);
						pack = NULL;
						state = 0;
					}
				}
				else
					state = 0;
				break;

			//Read memory address
			case 3:

				if(s.m_stype == I2CSymbol::TYPE_DATA)
				{
					//Grab additional address bits
					ptr = (ptr << 8) | s.m_data;
					addr_count ++;

					//Wait for ACK/NAK
					state = 4;
				}

				//Stop right after device address is a polling ping
				else if( (s.m_stype == I2CSymbol::TYPE_STOP) && (addr_count == 0) )
				{
					cap->m_samples[ntype].m_type = I2CEepromSymbol::TYPE_POLL_OK;
					pack->m_headers["Type"] = "Poll - OK";
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_STATUS];
					m_packets.push_back(pack);
					pack = NULL;
					state = 0;
				}

				else
					state = 0;
				break;

			//Expect ACK/NAK for address byte
			case 4:
				if(s.m_stype == I2CSymbol::TYPE_ACK)
				{
					//Abort if NAK
					if(s.m_data)
						state = 0;

					//ACK. Was this the last address byte?
					else
					{
						//Yes, create the sample and move on to data
						if( (addr_count*8) >= pointer_bits)
						{
							cap->m_offsets.push_back(tstart);
							cap->m_durations.push_back(end - tstart);
							cap->m_samples.push_back(I2CEepromSymbol(I2CEepromSymbol::TYPE_ADDRESS, ptr));
							tstart = end;
							state = 5;

							char tmp[128];
							if(raw_bits > 16)
								snprintf(tmp, sizeof(tmp), "%05x", ptr);
							else if(raw_bits > 12)
								snprintf(tmp, sizeof(tmp), "%04x", ptr);
							else if(raw_bits > 8)
								snprintf(tmp, sizeof(tmp), "%03x", ptr);
							else if(raw_bits > 4)
								snprintf(tmp, sizeof(tmp), "%02x", ptr);
							else
								snprintf(tmp, sizeof(tmp), "%01x", ptr);

							pack->m_headers["Address"] = tmp;
						}

						//No, more address bytes to follow
						else
							state = 3;
					}
				}
				else
					state = 0;
				break;

			//Expect restart before moving to data for reads.
			//For writes, this is the first data byte.
			case 5:
				if(s.m_stype == I2CSymbol::TYPE_RESTART)
				{
					cap->m_samples[ntype].m_type = I2CEepromSymbol::TYPE_SELECT_READ;
					state = 6;
					pack->m_headers["Type"] = "Read";
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
				}
				else if(s.m_stype == I2CSymbol::TYPE_DATA)
				{
					//Data right after without a restart? This is a write data byte.
					cap->m_offsets.push_back(tstart);
					cap->m_durations.push_back(end - tstart);
					cap->m_samples.push_back(I2CEepromSymbol(I2CEepromSymbol::TYPE_DATA, s.m_data));
					tstart = end;

					//Save the data byte
					pack->m_data.push_back(s.m_data);

					//Expect an ACK right after.
					state = 9;

					//Update type of the transaction
					cap->m_samples[ntype].m_type = I2CEepromSymbol::TYPE_SELECT_WRITE;
					pack->m_headers["Type"] = "Write";
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
				}
				else
					state = 0;
				break;

			//Expect address word for our read address
			case 6:
				if(s.m_stype == I2CSymbol::TYPE_ADDRESS)
				{
					//Should be for the same address
					if( (s.m_data & 0xfe) != (last_device_addr & 0xfe) )
						state = 0;

					//Expect read bit set, no sense in restarting with a write
					else if( (s.m_data & 1) == 0)
						state = 0;

					//Correct address. Expect an ACK/NAK after this
					else
						state = 7;
				}
				else
					state = 0;
				break;

			//Expect ACK/NAK after read address
			case 7:
				if(s.m_stype == I2CSymbol::TYPE_ACK)
				{
					//Abort if NAK
					if(s.m_data)
						state = 0;

					//Device selected for readback.
					//Extend the address sample to now, then start with read data
					else
					{
						size_t nlast = cap->m_offsets.size() - 1;
						cap->m_durations[nlast] = end - cap->m_offsets[nlast];
						tstart = end;
						state = 8;
					}
				}
				else
					state = 0;
				break;

			//Expect a read/write data byte
			case 8:
				if(s.m_stype == I2CSymbol::TYPE_DATA)
				{
					cap->m_offsets.push_back(tstart);
					cap->m_durations.push_back(end - tstart);
					cap->m_samples.push_back(I2CEepromSymbol(I2CEepromSymbol::TYPE_DATA, s.m_data));

					pack->m_data.push_back(s.m_data);
					state = 9;
				}
				else
				{
					if(s.m_stype == I2CSymbol::TYPE_STOP)
					{
						m_packets.push_back(pack);
						char tmp[128];
						snprintf(tmp, sizeof(tmp), "%zu", pack->m_data.size());
						pack->m_headers["Len"] = tmp;
						pack = NULL;
					}
					state = 0;
				}
				break;

			//Expect an ACK/NAK
			case 9:
				if(s.m_stype == I2CSymbol::TYPE_ACK)
				{
					//Extend last sample
					size_t nlast = cap->m_offsets.size() - 1;
					cap->m_durations[nlast] = end - cap->m_offsets[nlast];
					tstart = end;

					//Done if NAK.
					//Otherwise move on to the next data byte
					if(s.m_data)
					{
						state = 0;
						char tmp[128];
						snprintf(tmp, sizeof(tmp), "%zu", pack->m_data.size());
						pack->m_headers["Len"] = tmp;
						m_packets.push_back(pack);
						pack = NULL;
					}
					else
						state = 8;
				}

				else
					state = 0;
				break;
		}
	}

	if(pack)
		delete pack;

	SetData(cap, 0);
}

Gdk::Color I2CEepromDecoder::GetColor(int i)
{
	auto capture = dynamic_cast<I2CEepromWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const I2CEepromSymbol& s = capture->m_samples[i];

		switch(s.m_type)
		{
			case I2CEepromSymbol::TYPE_SELECT_READ:
			case I2CEepromSymbol::TYPE_SELECT_WRITE:
				return m_standardColors[COLOR_CONTROL];

			case I2CEepromSymbol::TYPE_POLL_BUSY:
				return m_standardColors[COLOR_IDLE];

			case I2CEepromSymbol::TYPE_POLL_OK:
				return m_standardColors[COLOR_CHECKSUM_OK];

			case I2CEepromSymbol::TYPE_ADDRESS:
				return m_standardColors[COLOR_ADDRESS];

			case I2CEepromSymbol::TYPE_DATA:
				return m_standardColors[COLOR_DATA];

			default:
				return m_standardColors[COLOR_ERROR];
		}
	}

	return m_standardColors[COLOR_ERROR];
}

string I2CEepromDecoder::GetText(int i)
{
	int raw_bits = m_parameters[m_memtypename].GetIntVal();

	auto capture = dynamic_cast<I2CEepromWaveform*>(GetData(0));
	char tmp[128] = "";
	if(capture != NULL)
	{
		const I2CEepromSymbol& s = capture->m_samples[i];

		switch(s.m_type)
		{
			case I2CEepromSymbol::TYPE_SELECT_READ:
				return "Read";

			case I2CEepromSymbol::TYPE_SELECT_WRITE:
				return "Write";

			case I2CEepromSymbol::TYPE_POLL_BUSY:
				return "Busy";

			case I2CEepromSymbol::TYPE_POLL_OK:
				return "Ready";

			case I2CEepromSymbol::TYPE_ADDRESS:
				if(raw_bits > 16)
					snprintf(tmp, sizeof(tmp), "Addr: %05x", s.m_data);
				else if(raw_bits > 12)
					snprintf(tmp, sizeof(tmp), "Addr: %04x", s.m_data);
				else if(raw_bits > 8)
					snprintf(tmp, sizeof(tmp), "Addr: %03x", s.m_data);
				else if(raw_bits > 4)
					snprintf(tmp, sizeof(tmp), "Addr: %02x", s.m_data);
				else
					snprintf(tmp, sizeof(tmp), "Addr: %01x", s.m_data);
				break;

			case I2CEepromSymbol::TYPE_DATA:
				snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
				break;

			default:
				return "";
		}
	}

	return string(tmp);
}

bool I2CEepromDecoder::CanMerge(Packet* first, Packet* /*cur*/, Packet* next)
{
	//Merge polling packets
	if( (first->m_headers["Type"].find("Poll") == 0) && (next->m_headers["Type"].find("Poll") == 0 ) )
		return true;

	return false;
}

Packet* I2CEepromDecoder::CreateMergedHeader(Packet* pack, size_t /*i*/)
{
	if(pack->m_headers["Type"].find("Poll")  == 0)
	{
		Packet* ret = new Packet;
		ret->m_offset = pack->m_offset;
		ret->m_len = pack->m_len;				//TODO: extend?
		ret->m_headers["Type"] = "Poll";
		ret->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_STATUS];

		//TODO: add other fields?
		return ret;
	}

	return NULL;
}
