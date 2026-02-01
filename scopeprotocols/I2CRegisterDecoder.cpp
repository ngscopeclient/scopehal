/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
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
#include "I2CRegisterDecoder.h"
#include "I2CDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

I2CRegisterDecoder::I2CRegisterDecoder(const string& color)
	: PacketDecoder(color, CAT_BUS)
	, m_addrbytes(m_parameters["Address Bytes"])
	, m_baseaddr(m_parameters["Bus Address"])
{
	CreateInput("i2c");

	m_addrbytes = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	for(int i=1; i<=4; i++)
		m_addrbytes.AddEnumValue(to_string(i), i);
	m_addrbytes.SetIntVal(1);

	m_baseaddr = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_HEXNUM));
	m_baseaddr.SetIntVal(0x90);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool I2CRegisterDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i == 0) && (dynamic_cast<I2CWaveform*>(stream.m_channel->GetData(0)) != nullptr) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

vector<string> I2CRegisterDecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("Type");
	ret.push_back("Address");
	ret.push_back("Len");
	return ret;
}

string I2CRegisterDecoder::GetProtocolName()
{
	return "I2C Register";
}

Filter::DataLocation I2CRegisterDecoder::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void I2CRegisterDecoder::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("I2CRegisterDecoder::Refresh");
	#endif

	ClearPackets();

	//Make sure we've got valid inputs
	ClearErrors();
	auto din = dynamic_cast<I2CWaveform*>(GetInputWaveform(0));
	if(!din)
	{
		if(!GetInput(0))
			AddErrorMessage("Missing inputs", "No signal input connected");
		else if(!GetInputWaveform(0))
			AddErrorMessage("Missing inputs", "No waveform available at input");
		else
			AddErrorMessage("Invalid input", "Expected an I2C waveform");

		SetData(nullptr, 0);
		return;
	}

	din->PrepareForCpuAccess();

	//Pull out our settings
	uint8_t base_addr = m_baseaddr.GetIntVal();
	int pointer_bytes = m_addrbytes.GetIntVal();

	//Set up output
	auto cap = SetupEmptyWaveform<I2CRegisterWaveform>(din, 0);
	cap->SetAddrBytes(pointer_bytes);
	cap->PrepareForCpuAccess();

	//Main decode loop
	I2CRegisterSymbol samp;
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
					if( (s.m_data & 0xfe) != base_addr)
					{
						state = 0;
						continue;
					}

					last_device_addr = s.m_data;

					//We should always be an I2C write (setting address pointer) even if reading data
					if(s.m_data & 1)
						state = 0;

					//Expect ACK/NAK then move on
					else
					{
						cap->m_offsets.push_back(tstart);
						cap->m_durations.push_back(end - tstart);
						cap->m_samples.push_back(I2CRegisterSymbol(I2CRegisterSymbol::TYPE_SELECT_READ, 0));
						state = 2;

						tstart = end;
					}
				}
				else
					state = 0;
				break;

			//Expect an ACK and extend the device address if no device bits.
			case 2:
				if(s.m_stype == I2CSymbol::TYPE_ACK)
				{
					//Extend the address sample as needed
					size_t nlast = cap->m_offsets.size() - 1;
					cap->m_durations[nlast] += din->m_durations[i];
					tstart += din->m_durations[i];

					//Move on to the memory address
					state = 3;
					ptr = 0;
					addr_count = 0;
					ntype = nlast;

					//If NAK, discard the transaction
					if(s.m_data)
					{
						delete pack;
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
						if( addr_count >= pointer_bytes)
						{
							cap->m_offsets.push_back(tstart);
							cap->m_durations.push_back(end - tstart);
							cap->m_samples.push_back(I2CRegisterSymbol(I2CRegisterSymbol::TYPE_ADDRESS, ptr));
							tstart = end;
							state = 5;

							char tmp[128];
							switch(pointer_bytes)
							{
								case 1:
									snprintf(tmp, sizeof(tmp), "%02x", ptr);
									break;
								case 2:
									snprintf(tmp, sizeof(tmp), "%04x", ptr);
									break;
								case 3:
									snprintf(tmp, sizeof(tmp), "%06x", ptr);
									break;
								case 4:
									snprintf(tmp, sizeof(tmp), "%08x", ptr);
									break;
							}
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
			//Stop/start pair is also legal.
			case 5:
				if(s.m_stype == I2CSymbol::TYPE_STOP)
				{}
				else if( (s.m_stype == I2CSymbol::TYPE_RESTART) || (s.m_stype == I2CSymbol::TYPE_START) )
				{
					cap->m_samples[ntype].m_type = I2CRegisterSymbol::TYPE_SELECT_READ;
					state = 6;
					pack->m_headers["Type"] = "Read";
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
				}
				else if(s.m_stype == I2CSymbol::TYPE_DATA)
				{
					//Data right after without a restart? This is a write data byte.
					cap->m_offsets.push_back(tstart);
					cap->m_durations.push_back(end - tstart);
					cap->m_samples.push_back(I2CRegisterSymbol(I2CRegisterSymbol::TYPE_DATA, s.m_data));
					tstart = end;

					//Save the data byte
					pack->m_data.push_back(s.m_data);

					//Expect an ACK right after.
					state = 9;

					//Update type of the transaction
					cap->m_samples[ntype].m_type = I2CRegisterSymbol::TYPE_SELECT_WRITE;
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
					cap->m_samples.push_back(I2CRegisterSymbol(I2CRegisterSymbol::TYPE_DATA, s.m_data));

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

	cap->MarkModifiedFromCpu();
}

string I2CRegisterWaveform::GetColor(size_t i)
{
	const I2CRegisterSymbol& s = m_samples[i];

	switch(s.m_type)
	{
		case I2CRegisterSymbol::TYPE_SELECT_READ:
		case I2CRegisterSymbol::TYPE_SELECT_WRITE:
			return StandardColors::colors[StandardColors::COLOR_CONTROL];

		case I2CRegisterSymbol::TYPE_ADDRESS:
			return StandardColors::colors[StandardColors::COLOR_ADDRESS];

		case I2CRegisterSymbol::TYPE_DATA:
			return StandardColors::colors[StandardColors::COLOR_DATA];

		default:
			return StandardColors::colors[StandardColors::COLOR_ERROR];
	}
}

string I2CRegisterWaveform::GetText(size_t i)
{
	char tmp[128] = "";
	const I2CRegisterSymbol& s = m_samples[i];

	switch(s.m_type)
	{
		case I2CRegisterSymbol::TYPE_SELECT_READ:
			return "Read";

		case I2CRegisterSymbol::TYPE_SELECT_WRITE:
			return "Write";

		case I2CRegisterSymbol::TYPE_ADDRESS:
			{
				switch(m_addrBytes)
				{
					case 1:
						snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
						break;
					case 2:
						snprintf(tmp, sizeof(tmp), "%04x", s.m_data);
						break;
					case 3:
						snprintf(tmp, sizeof(tmp), "%06x", s.m_data);
						break;
					case 4:
						snprintf(tmp, sizeof(tmp), "%08x", s.m_data);
						break;
				}
			}
			break;

		case I2CRegisterSymbol::TYPE_DATA:
			snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
			break;

		default:
			return "";
	}

	return tmp;
}
