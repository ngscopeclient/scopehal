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

MDIODecoder::MDIODecoder(const string& color)
	: PacketDecoder(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_SERIAL)
{
	//Set up channels
	CreateInput("mdio");
	CreateInput("mdc");

	m_typename = "PHY Type";
	m_parameters[m_typename] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_typename].AddEnumValue("Generic", PHY_TYPE_GENERIC);
	m_parameters[m_typename].AddEnumValue("KSZ9031", PHY_TYPE_KSZ9031);
	m_parameters[m_typename].SetIntVal(PHY_TYPE_GENERIC);
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

	if( (i < 2) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) )
		return true;

	return false;
}

string MDIODecoder::GetProtocolName()
{
	return "MDIO";
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

	int phytype = m_parameters[m_typename].GetIntVal();

	//Create the capture
	auto cap = new MDIOWaveform;
	cap->m_timescale = 1;	//SampleOnRisingEdges() gives us fs level timestamps
	cap->m_startTimestamp = mdc->m_startTimestamp;
	cap->m_startFemtoseconds = mdc->m_startFemtoseconds;

	//Maintain MMD state across transactions
	int mmd_dev = 0;
	bool mmd_is_reg = false;

	//Sample the data stream at each clock edge
	DigitalWaveform dmdio;
	SampleOnRisingEdges(mdio, mdc, dmdio);
	size_t dlen = dmdio.m_samples.size();
	for(size_t i=0; i<dlen; i++)
	{
		//Abort if we don't have space for a whole frame
		if(i + 63 >= dlen)
		{
			LogDebug("aborting at i=%zu, %s\n", i, Unit(Unit::UNIT_FS).PrettyPrint(dmdio.m_offsets[i]).c_str());
			break;
		}

		//Start by looking for a preamble
		size_t start = dmdio.m_offsets[i];
		bool err = false;
		size_t end = 0;
		for(size_t j=0; j<32; j++)
		{
			end = dmdio.m_offsets[i+j] + dmdio.m_durations[i+j];

			//Expect 32 "1" bits in a row. If we see any non-1 bits, declare an error
			if(dmdio.m_samples[i+j] != true)
			{
				LogDebug("Err: some 0 bits\n");
				err = true;
				break;
			}
		}
		size_t len = end - start;

		//If we don't have a valid preamble, move on
		if(err)
			continue;

		//The first bit in the SOF has to be a 0.
		//If it's not, we've got an overly long preamble (>32 bits), so wait until we get a real SOF.
		if(dmdio.m_samples[i+32])
			continue;

		i += 32;

		//Good preamble
		cap->m_offsets.push_back(start);
		cap->m_durations.push_back(len);
		cap->m_samples.push_back(MDIOSymbol(MDIOSymbol::TYPE_PREAMBLE, 0));

		//Create the packet
		Packet* pack = new Packet;
		pack->m_offset = start;

		//Next 2 bits are start delimiter
		if(i+2 > dlen)
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
			if(i+2 > dlen)
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
			{
				pack->m_headers["Op"] = "Write";
				pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
			}
			else if(op == 2)
			{
				pack->m_headers["Op"] = "Read";
				pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
			}
			else
				pack->m_headers["Op"] = "ERROR";

			cap->m_offsets.push_back(dmdio.m_offsets[i]);
			cap->m_durations.push_back((dmdio.m_offsets[i+1] - dmdio.m_offsets[i]) + dmdio.m_durations[i+1]);
			cap->m_samples.push_back(MDIOSymbol(MDIOSymbol::TYPE_OP, op));
			i += 2;

			//Next 5 bits are PHY address
			if(i+5 > dlen)
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
			if(i+5 > dlen)
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
			if(i+2 > dlen)
				break;
			cap->m_offsets.push_back(dmdio.m_offsets[i]);
			cap->m_durations.push_back((dmdio.m_offsets[i+1] - dmdio.m_offsets[i]) + dmdio.m_durations[i+1]);
			cap->m_samples.push_back(MDIOSymbol(MDIOSymbol::TYPE_TURN, 0));
			i += 2;

			//Next 16 bits are frame data
			if(i+16 > dlen)
			{
				delete pack;
				break;
			}
			uint16_t value = 0;
			start = dmdio.m_offsets[i];
			for(size_t j=0; j<16; j++)
			{
				//Use previous clock cycle's duration for last sample
				//rather than stretching until next clock edge
				if(j == 15)
					len = (dmdio.m_offsets[i+j] - start) + dmdio.m_durations[i+j-1];
				else
					len = (dmdio.m_offsets[i+j] - start) + dmdio.m_durations[i+j];

				value <<= 1;
				if(dmdio.m_samples[i+j])
					value |= 1;
			}
			cap->m_offsets.push_back(start);
			cap->m_durations.push_back(len);
			cap->m_samples.push_back(MDIOSymbol(MDIOSymbol::TYPE_DATA, value));
			i += 15;	//next increment will be done by the i++ at the top of the loop

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
						info = "Basic Control: ";

						if(value & 0x8000)
							info += "Reset ";
						if(value & 0x4000)
							info += "Loopback ";
						if(value & 0x0400)
							info += "Isolate ";
						if(value & 0x0200)
							info += "AnegRestart ";

						uint8_t speed = 0;
						if(value & 0x0040)
							speed |= 2;
						if(value & 0x2000)
							speed |= 1;

						switch(speed)
						{
							case 0:
								info += "10M";
								break;

							case 1:
								info += "100M";
								break;

							case 2:
								info += "1G";
								break;

							default:
								info += "BadSpeed";
								break;
						}

						if(value & 0x0100)
							info += "/full ";
						else
							info += "/half ";

						if( (value & 0x1000) == 0)
							info += "AnegDisable ";

						if(value & 0x0800)
							info += "PowerDown ";
					}
					break;

				//802.3 Basic Status
				case 0x1:
					info = "Basic Status: ";

					if(value & 0x4)
						info += "Up ";
					else
						info += "Down ";
					if(value & 0x20)
						info += "AnegDone ";

					if(value & 0x0100)
						info += "ExtStatus ";
					if(value & 0x01)
						info += "ExtCaps ";

					if(value & 0x0040)
						info += "PreambleSupp ";
					if(value & 0x10)
						info += "RemoteFault ";
					if(value & 0x08)
						info += "AnegCapable ";
					if(value & 0x02)
						info += "JabberDetect ";

					info += "PMAs: ";
					if(value & 0x8000)
						info += "100baseT4 ";
					if(value & 0x4000)
						info += "100baseTX/full ";
					if(value & 0x2000)
						info += "100baseTX/half ";
					if(value & 0x1000)
						info += "10baseT/full ";
					if(value & 0x0800)
						info += "10baseT/half ";

					break;

				//PHY ID
				case 0x2:
					info = "PHY ID 1";
					switch(phytype)
					{
						case PHY_TYPE_KSZ9031:
							if(value != 0x0022)
								info += ": ERROR, should be 0x0022 for KSZ9031";
							else
								info += ": Kendin/Micrel/Microchip";
							break;

						default:
							break;
					}

					break;
				case 0x3:
					info = "PHY ID 2";
					switch(phytype)
					{
						case PHY_TYPE_KSZ9031:
							if( ((value >> 10) & 0x3f) != 0x5)
								info += ": ERROR, vendor ID should be 0x5 for KSZ9031";
							else
							{
								if( ((value >> 4) & 0x3f) != 0x22)
									info += ": ERROR, model ID should be 0x22 for KSZ9031";
								else
									info += string(": KSZ9031 stepping ") + to_string(value & 0xf);
							}
							break;

						default:
							break;
					}
					break;

				//Autonegotiation
				case 0x4:
					info = "ANEG Advertisement: ";
					if( (value & 0x1F) != 1)
						info += "NotEthernet ";
					if(value & 0x8000)
						info += "NextPage ";
					if(value & 0x2000)
						info += "RemFltSupp ";
					if(value & 0x0800)
						info += "AsymPause ";
					if(value & 0x0400)
						info += "SymPause ";
					if(value & 0x0200)
						info += "100baseT4 ";
					if(value & 0x0100)
						info += "100baseTX/full ";
					if(value & 0x0080)
						info += "100baseTX/half ";
					if(value & 0x0040)
						info += "10baseTX/full ";
					if(value & 0x0020)
						info += "10baseTX/half ";
					break;
				case 0x5:
					info = "ANEG Partner Ability";

					if( (value & 0x1F) != 1)
						info += "NotEthernet ";
					if(value & 0x8000)
						info += "NextPage ";
					if(value & 0x4000)
						info += "ACK ";
					if(value & 0x2000)
						info += "RemoteFault ";
					if(value & 0x0800)
						info += "AsymPause ";
					if(value & 0x0400)
						info += "SymPause ";
					if(value & 0x0200)
						info += "100baseT4 ";
					if(value & 0x0100)
						info += "100baseTX/full ";
					if(value & 0x0080)
						info += "100baseTX/half ";
					if(value & 0x0040)
						info += "10baseTX/full ";
					if(value & 0x0020)
						info += "10baseTX/half ";
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
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_CONTROL];
					mmd_is_reg = false;

					switch(value >> 14)
					{
						case 0:
							info += "Register";
							mmd_is_reg = true;
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

					mmd_dev = (value & 0x1f);
					snprintf(tmp, sizeof(tmp), "%02x", mmd_dev);
					info += string(", MMD device = ") + tmp;
					break;

				case 0xe:
					if(mmd_is_reg)
					{
						info = "MMD Address";
						pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_CONTROL];
					}
					else
						info = "MMD Data";
					break;

				case 0xf:
					info = "Extended Status: ";
					if(value & 0x8000)
						info += "1000base-X/full ";
					if(value & 0x4000)
						info += "1000base-X/half ";
					if(value & 0x2000)
						info += "1000base-T/full ";
					if(value & 0x1000)
						info += "1000base-T/half ";
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

bool MDIODecoder::CanMerge(Packet* first, Packet* /*cur*/, Packet* next)
{
	//If different PHYs, obviously can't merge
	if(first->m_headers["PHY"] != next->m_headers["PHY"])
		return false;

	//Start merging when we get an access to the MMD address register
	if( (first->m_headers["Reg"] == "0d") && (first->m_headers["Info"].find("Register") != string::npos) )
	{
		//Only merge accesses to 0e or 0d-with-data
		if(next->m_headers["Reg"] == "0e")
			return true;

		if( (next->m_headers["Reg"] == "0d") && (next->m_headers["Info"].find("Data") != string::npos) )
			return true;
	}

	return false;
}

Packet* MDIODecoder::CreateMergedHeader(Packet* pack, size_t i)
{
	Packet* ret = new Packet;
	ret->m_offset = pack->m_offset;
	ret->m_len = pack->m_len;

	//Default to copying everything from the first packet
	ret->m_headers["Clause"] = pack->m_headers["Clause"];
	ret->m_headers["Op"] = pack->m_headers["Op"];
	ret->m_headers["PHY"] = pack->m_headers["PHY"];
	ret->m_headers["Reg"] = pack->m_headers["Reg"];
	ret->m_headers["Value"] = pack->m_headers["Value"];
	ret->m_headers["Info"] = pack->m_headers["Info"];
	ret->m_displayBackgroundColor = pack->m_displayBackgroundColor;

	int phytype = m_parameters[m_typename].GetIntVal();

	//Search forward until we find the actual MMD data access, then update our color/type based on that
	unsigned int mmd_reg_addr = 0;
	unsigned int mmd_device = 0;
	unsigned int mmd_value = 0;
	bool mmd_is_addr = false;
	for(size_t j=i; j<m_packets.size(); j++)
	{
		//Check type field
		auto p = m_packets[j];
		unsigned int pvalue = strtol(p->m_headers["Value"].c_str(), NULL, 16);

		//Decode address info
		if(p->m_headers["Reg"] == "0d")
		{
			if(p->m_headers["Info"].find("Register") != string::npos)
				mmd_is_addr = true;
			else
				mmd_is_addr = false;

			mmd_device = pvalue & 0x1f;
		}

		if(p->m_headers["Reg"] == "0e")
		{
			if(mmd_is_addr)
				mmd_reg_addr = pvalue;

			//Figure out top level op type on the final data transaction
			else
			{
				ret->m_headers["Op"] = p->m_headers["Op"];
				ret->m_headers["Reg"] = p->m_headers["Reg"];
				ret->m_headers["Value"] = p->m_headers["Value"];
				ret->m_displayBackgroundColor = p->m_displayBackgroundColor;

				mmd_value = pvalue;
				break;
			}
		}
	}

	//Default for unknown PHY type or unknown register
	char tmp[128];
	snprintf(tmp, sizeof(tmp), "MMD %02x reg %04x = %04x", mmd_device, mmd_reg_addr, mmd_value);
	string info = tmp;

	switch(phytype)
	{
		case PHY_TYPE_KSZ9031:
			switch(mmd_device)
			{
				case 0x00:
					switch(mmd_reg_addr)
					{
						case 3:
							info = "AN FLP Timer Lo: ";
							if(mmd_value == 0x1a80)
								info += "16 ms";
							else if(mmd_value == 0x4000)
								info += "8 ms";
							else
								info += "Reserved";
							break;

						case 4:
							info = "AN FLP Timer Hi: ";
							if(mmd_value == 0x3)
								info += "8 ms";
							else if(mmd_value == 0x6)
								info += "16 ms";
							else
								info += "Reserved";
							break;
					}
					break;

				case 0x1:
					break;

				case 0x2:
					break;

				case 0x1c:
					if(mmd_reg_addr == 0x23)
					{
						//EDPD Control
						info = "EDPD Control: ";
						if(mmd_value & 1)
							info += "Enable";
						else
							info += "Disable";
					}
					break;
			}
			break;

		default:
			break;
	}

	ret->m_headers["Info"] = info;

	return ret;
}
