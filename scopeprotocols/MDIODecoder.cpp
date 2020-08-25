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
	@brief Implementation of MDIODecoder
 */

#include "../scopehal/scopehal.h"
#include "MDIODecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

MDIODecoder::MDIODecoder(string color)
	: PacketDecoder(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_SERIAL)
{
	//Set up channels
	CreateInput("mdio");
	CreateInput("mdc");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool MDIODecoder::NeedsConfig()
{
	return true;
}

bool MDIODecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 2) &&
		(stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) &&
		(stream.m_channel->GetWidth() == 1)
		)
	{
		return true;
	}

	return false;
}

string MDIODecoder::GetProtocolName()
{
	return "MDIO";
}

void MDIODecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "MDIO(%s, %s)",
		GetInputDisplayName(0).c_str(),
		GetInputDisplayName(1).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void MDIODecoder::Refresh()
{
	char tmp[128];

	//Remove old packets from previous decode passes
	ClearPackets();

	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto mdio = GetDigitalInputWaveform(0);
	auto mdc = GetDigitalInputWaveform(1);

	//Create the capture
	auto cap = new MDIOWaveform;
	cap->m_timescale = 1;	//SampleOnRisingEdges() gives us ps level timestamps
	cap->m_startTimestamp = mdc->m_startTimestamp;
	cap->m_startPicoseconds = mdc->m_startPicoseconds;

	//Sample the data stream at each clock edge
	DigitalWaveform dmdio;
	SampleOnRisingEdges(mdio, mdc, dmdio);
	size_t dlen = dmdio.m_samples.size();
	for(size_t i=0; i<dlen; i++)
	{
		//Start by looking for a preamble
		size_t start = dmdio.m_offsets[i];
		bool err = false;
		size_t len = 0;
		for(size_t j=0; j<32; j++)
		{
			len = (dmdio.m_offsets[i+j] - start) + dmdio.m_durations[i+j];

			//Abort if we don't have space for a whole frame
			if(i+j+1 == len)
			{
				err = true;
				break;
			}

			//Expect 32 "1" bits in a row. If we see any non-1 bits, declare an error
			if(dmdio.m_samples[i+j] != true)
			{
				err = true;
				break;
			}
		}
		i += 32;

		//If we don't have a valid preamble, stop
		if(err)
		{
			cap->m_offsets.push_back(start);
			cap->m_durations.push_back(len);
			cap->m_samples.push_back(MDIOSymbol(MDIOSymbol::TYPE_ERROR, 0));
			continue;
		}

		//Good preamble
		cap->m_offsets.push_back(start);
		cap->m_durations.push_back(len);
		cap->m_samples.push_back(MDIOSymbol(MDIOSymbol::TYPE_PREAMBLE, 0));

		//Create the packet
		Packet* pack = new Packet;
		pack->m_offset = start;

		//TODO: safely ignore extra preamble bits

		//Next 2 bits are start delimiter
		if(i+2 >= dlen)
		{
			delete pack;
			break;
		}
		uint16_t sof = 0;
		if(dmdio.m_samples[i])
			sof |= 2;
		if(dmdio.m_samples[i+1])
			sof |= 1;

		//MDIO Clause 22 frame
		if(sof == 0x01)
		{
			pack->m_headers["Clause"] = "22";

			//Add the start symbol
			cap->m_offsets.push_back(dmdio.m_offsets[i]);
			cap->m_durations.push_back((dmdio.m_offsets[i+1] - dmdio.m_offsets[i]) + dmdio.m_durations[i+1]);
			cap->m_samples.push_back(MDIOSymbol(MDIOSymbol::TYPE_START, sof));
			i += 2;

			//Next 2 bits are opcode
			if(i+2 >= dlen)
			{
				delete pack;
				break;
			}
			uint16_t op = 0;
			if(dmdio.m_samples[i])
				op |= 2;
			if(dmdio.m_samples[i+1])
				op |= 1;

			if(op == 1)
				pack->m_headers["Op"] = "Write";
			else if(op == 2)
				pack->m_headers["Op"] = "Read";
			else
				pack->m_headers["Op"] = "ERROR";

			cap->m_offsets.push_back(dmdio.m_offsets[i]);
			cap->m_durations.push_back((dmdio.m_offsets[i+1] - dmdio.m_offsets[i]) + dmdio.m_durations[i+1]);
			cap->m_samples.push_back(MDIOSymbol(MDIOSymbol::TYPE_OP, op));
			i += 2;

			//Next 5 bits are PHY address
			if(i+5 >= dlen)
				break;
			uint16_t addr = 0;
			start = dmdio.m_offsets[i];
			for(size_t j=0; j<5; j++)
			{
				len = (dmdio.m_offsets[i+j] - start) + dmdio.m_durations[i+j];
				addr <<= 1;
				if(dmdio.m_samples[i+j])
					addr |= 1;
			}
			cap->m_offsets.push_back(start);
			cap->m_durations.push_back(len);
			cap->m_samples.push_back(MDIOSymbol(MDIOSymbol::TYPE_PHYADDR, addr));
			i += 5;

			snprintf(tmp, sizeof(tmp), "%02x", addr);
			pack->m_headers["PHY"] = tmp;

			//Next 5 bits are reg address
			if(i+5 >= dlen)
			{
				delete pack;
				break;
			}
			addr = 0;
			start = dmdio.m_offsets[i];
			for(size_t j=0; j<5; j++)
			{
				len = (dmdio.m_offsets[i+j] - start) + dmdio.m_durations[i+j];
				addr <<= 1;
				if(dmdio.m_samples[i+j])
					addr |= 1;
			}
			cap->m_offsets.push_back(start);
			cap->m_durations.push_back(len);
			cap->m_samples.push_back(MDIOSymbol(MDIOSymbol::TYPE_REGADDR, addr));
			i += 5;

			snprintf(tmp, sizeof(tmp), "%02x", addr);
			pack->m_headers["Reg"] = tmp;

			//Next 2 bits are bus turnaround
			if(i+2 >= dlen)
				break;
			cap->m_offsets.push_back(dmdio.m_offsets[i]);
			cap->m_durations.push_back((dmdio.m_offsets[i+1] - dmdio.m_offsets[i]) + dmdio.m_durations[i+1]);
			cap->m_samples.push_back(MDIOSymbol(MDIOSymbol::TYPE_TURN, 0));
			i += 2;

			//Next 16 bits are frame data
			if(i+16 >= dlen)
			{
				delete pack;
				break;
			}
			uint16_t value = 0;
			start = dmdio.m_offsets[i];
			for(size_t j=0; j<16; j++)
			{
				len = (dmdio.m_offsets[i+j] - start) + dmdio.m_durations[i+j];
				value <<= 1;
				if(dmdio.m_samples[i+j])
					value |= 1;
			}
			cap->m_offsets.push_back(start);
			cap->m_durations.push_back(len);
			cap->m_samples.push_back(MDIOSymbol(MDIOSymbol::TYPE_DATA, value));
			i += 16;

			snprintf(tmp, sizeof(tmp), "%04x", value);
			pack->m_headers["Value"] = tmp;

			//Add extra information to the decode if it's a known register
			//TODO: share this between clause 22 and 45 decoders
			string info;
			switch(addr)
			{
				//802.3 Basic Control
				case 0x00:
					{
						info = "Basic Status: ";

						uint8_t speed = 0;
						if(value & 0x0040)
							speed |= 2;
						if(value & 0x2000)
							speed |= 1;

						switch(speed)
						{
							case 0:
								info += "Speed 10M";
								break;

							case 1:
								info += "Speed 100M";
								break;

							case 2:
								info += "Speed 1G";
								break;

							default:
								info += "Speed invalid";
								break;
						}

						if( (value & 0x0100) == 0)
							info += "/full";
						else
							info += "/half";

						if( (value & 0x1000) == 0)
							info += ", Aneg disable";

						if( (value & 0x0800) == 0)
							info += ", Power down";
					}
					break;

				//802.3 Basic Status
				case 0x1:
					info = "Basic Status: ";

					if(value & 0x20)
						info += "Aneg complete";
					else
						info += "Aneg not complete";

					if(value & 0x4)
						info += ", Link up";
					else
						info += ", Link down";

					break;

				//PHY ID
				case 0x2:
					info = "PHY ID 1";
					break;
				case 0x3:
					info = "PHY ID 2";
					break;

				//Autonegotiation
				case 0x4:
					info = "ANEG Advertisement";
					break;
				case 0x5:
					info = "ANEG Partner Ability";
					break;
				case 0x6:
					info = "ANEG Expansion";
					break;
				case 0x7:
					info = "ANEG Next Page";
					break;
				case 0x8:
					info = "ANEG Partner Next Page";
					break;

				//1000base-T
				case 0x9:
					info = "1000base-T Control: ";
					if( (value >> 13) != 0)
					{
						snprintf(tmp, sizeof(tmp), "Test mode %d, ", value >> 13);
						info += tmp;
					}

					if(value & 0x1000)
					{
						if(value & 0x0800)
							info += "Force master";
						else
							info += "Force slave";
					}
					else
					{
						if(value & 0x0400)
							info += "Prefer master";
						else
							info += "Prefer slave";
					}
					break;

				case 0xa:
					info = "1000base-T Status: ";

					if(value & 0x4000)
						info += "Master, ";
					else
						info += "Slave, ";

					//TODO: other fields

					{
						snprintf(tmp, sizeof(tmp), "Err count: %d", value & 0xff);
						info += tmp;
					}

					break;

				//MMD stuff
				case 0xd:
					info = "MMD Access: ";

					switch(value >> 14)
					{
						case 0:
							info += "Register";
							break;

						case 1:
							info += "Data";
							break;

						case 2:
							info += "Data R/W increment";
							break;

						case 3:
							info += "Data W increment";
							break;
					}
					break;

				case 0xe:
					info = "MMD Addr/Data";
					break;

				case 0xf:
					info = "Extended Status";
					break;

				//TODO: support for PHY vendor specific registers if we know the PHY ID (or are told)
			}
			pack->m_headers["Info"] = info;

			//Done, add the packet
			m_packets.push_back(pack);
		}

		//MDIO Clause 45 frame
		else if(sof == 0x00)
		{
			LogWarning("MDIO Clause 45 not yet supported");
		}

		//Invalid frame format
		else
		{
			cap->m_offsets.push_back(dmdio.m_offsets[i]);
			cap->m_durations.push_back((dmdio.m_offsets[i+1] - dmdio.m_offsets[i]) + dmdio.m_offsets[i+1]);
			cap->m_samples.push_back(MDIOSymbol(MDIOSymbol::TYPE_ERROR, 0));
			continue;
		}
	}

	SetData(cap, 0);
}

vector<string> MDIODecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("Clause");
	ret.push_back("Op");
	ret.push_back("PHY");
	ret.push_back("Reg");
	ret.push_back("Value");
	ret.push_back("Info");
	return ret;
}

Gdk::Color MDIODecoder::GetColor(int i)
{
	auto capture = dynamic_cast<MDIOWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const MDIOSymbol& s = capture->m_samples[i];

		switch(s.m_stype)
		{
			case MDIOSymbol::TYPE_PREAMBLE:
			case MDIOSymbol::TYPE_START:
			case MDIOSymbol::TYPE_TURN:
				return m_standardColors[COLOR_PREAMBLE];

			case MDIOSymbol::TYPE_OP:
				if( (s.m_data == 1) || (s.m_data == 2) )
					return m_standardColors[COLOR_CONTROL];
				else
					return m_standardColors[COLOR_ERROR];

			case MDIOSymbol::TYPE_PHYADDR:
			case MDIOSymbol::TYPE_REGADDR:
				return m_standardColors[COLOR_ADDRESS];

			case MDIOSymbol::TYPE_DATA:
				return m_standardColors[COLOR_DATA];

			case MDIOSymbol::TYPE_ERROR:
				return m_standardColors[COLOR_ERROR];
		}
	}

	//error
	return Gdk::Color("red");
}

string MDIODecoder::GetText(int i)
{
	auto capture = dynamic_cast<MDIOWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const MDIOSymbol& s = capture->m_samples[i];

		char tmp[32];
		switch(s.m_stype)
		{
			case MDIOSymbol::TYPE_PREAMBLE:
				return "PREAMBLE";
			case MDIOSymbol::TYPE_START:
				return "SOF";
			case MDIOSymbol::TYPE_TURN:
				return "TURN";

			case MDIOSymbol::TYPE_OP:
				if(s.m_data == 1)
					return "WR";
				else if(s.m_data == 2)
					return "RD";
				else
					return "BAD OP";

			case MDIOSymbol::TYPE_PHYADDR:
				snprintf(tmp, sizeof(tmp), "PHY %02x", s.m_data);
				break;

			case MDIOSymbol::TYPE_REGADDR:
				snprintf(tmp, sizeof(tmp), "REG %02x", s.m_data);
				break;

			case MDIOSymbol::TYPE_DATA:
				snprintf(tmp, sizeof(tmp), "%04x", s.m_data);
				break;

			case MDIOSymbol::TYPE_ERROR:
			default:
				return "ERROR";
		}
		return string(tmp);
	}

	return "";
}
