/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2019 Andrew D. Zonenberg                                                                          *
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
#include "../scopehal/ChannelRenderer.h"
#include "../scopehal/TextRenderer.h"
#include "MDIORenderer.h"
#include "MDIODecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

MDIODecoder::MDIODecoder(string color)
	: PacketDecoder(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_SERIAL)
{
	//Set up channels
	m_signalNames.push_back("mdio");
	m_channels.push_back(NULL);

	m_signalNames.push_back("mdc");
	m_channels.push_back(NULL);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool MDIODecoder::NeedsConfig()
{
	return true;
}

ChannelRenderer* MDIODecoder::CreateRenderer()
{
	return new MDIORenderer(this);
}

bool MDIODecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) && (channel->GetWidth() == 1) )
		return true;
	if( (i == 1) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) && (channel->GetWidth() == 1) )
		return true;
	return false;
}

string MDIODecoder::GetProtocolName()
{
	return "MDIO";
}

void MDIODecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "MDIO(%s, %s)", m_channels[0]->m_displayname.c_str(), m_channels[1]->m_displayname.c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void MDIODecoder::Refresh()
{
	//Get the input data
	if( (m_channels[0] == NULL) || (m_channels[1] == NULL) )
	{
		SetData(NULL);
		return;
	}
	DigitalCapture* mdio = dynamic_cast<DigitalCapture*>(m_channels[0]->GetData());
	DigitalCapture* mdc = dynamic_cast<DigitalCapture*>(m_channels[1]->GetData());
	if( (mdio == NULL) || (mdc == NULL) )
	{
		SetData(NULL);
		return;
	}

	//Create the capture
	MDIOCapture* cap = new MDIOCapture;
	cap->m_timescale = 1;	//SampleOnRisingEdges() gives us ps level timestamps
	cap->m_startTimestamp = mdc->m_startTimestamp;
	cap->m_startPicoseconds = mdc->m_startPicoseconds;

	//Sample the data stream at each clock edge
	vector<DigitalSample> dmdio;
	SampleOnRisingEdges(mdio, mdc, dmdio);
	for(size_t i=0; i<dmdio.size(); i++)
	{
		//Start by looking for a preamble
		size_t start = dmdio[i].m_offset;
		bool err = false;
		size_t len = 0;
		for(size_t j=0; j<32; j++)
		{
			len = (dmdio[i+j].m_offset - start) + dmdio[i+j].m_duration;

			//Abort if we don't have space for a whole frame
			if(i+j+1 == dmdio.size())
			{
				err = true;
				break;
			}

			//Expect 32 "1" bits in a row. If we see any non-1 bits, declare an error
			if(dmdio[i+j].m_sample != true)
			{
				err = true;
				break;
			}
		}
		i += 32;

		//If we don't have a valid preamble, stop
		if(err)
		{
			cap->m_samples.push_back(MDIOSample(
				start,
				len,
				MDIOSymbol(MDIOSymbol::TYPE_ERROR, 0)));
			continue;
		}

		//Good preamble
		cap->m_samples.push_back(MDIOSample(
			start,
			len,
			MDIOSymbol(MDIOSymbol::TYPE_PREAMBLE, 0)));

		//Create the packet
		Packet* pack = new Packet;
		pack->m_offset = start;

		//TODO: safely ignore extra preamble bits

		//Next 2 bits are start delimiter
		if(i+2 >= dmdio.size())
		{
			delete pack;
			break;
		}
		uint16_t sof = 0;
		if(dmdio[i].m_sample)
			sof |= 2;
		if(dmdio[i+1].m_sample)
			sof |= 1;

		//MDIO Clause 22 frame
		if(sof == 0x01)
		{
			pack->m_headers["Clause"] = "22";

			//Add the start symbol
			cap->m_samples.push_back(MDIOSample(
				dmdio[i].m_offset,
				(dmdio[i+1].m_offset - dmdio[i].m_offset) + dmdio[i+1].m_duration,
				MDIOSymbol(MDIOSymbol::TYPE_START, sof)));
			i += 2;

			//Next 2 bits are opcode
			if(i+2 >= dmdio.size())
			{
				delete pack;
				break;
			}
			uint16_t op = 0;
			if(dmdio[i].m_sample)
				op |= 2;
			if(dmdio[i+1].m_sample)
				op |= 1;

			if(op == 1)
				pack->m_headers["Op"] = "Write";
			else if(op == 2)
				pack->m_headers["Op"] = "Read";
			else
				pack->m_headers["Op"] = "ERROR";

			cap->m_samples.push_back(MDIOSample(
				dmdio[i].m_offset,
				(dmdio[i+1].m_offset - dmdio[i].m_offset) + dmdio[i+1].m_duration,
				MDIOSymbol(MDIOSymbol::TYPE_OP, op)));
			i += 2;

			//Next 5 bits are PHY address
			if(i+5 >= dmdio.size())
				break;
			uint16_t addr = 0;
			start = dmdio[i].m_offset;
			for(size_t j=0; j<5; j++)
			{
				len = (dmdio[i+j].m_offset - start) + dmdio[i+j].m_duration;
				addr <<= 1;
				if(dmdio[i+j].m_sample)
					addr |= 1;
			}
			cap->m_samples.push_back(MDIOSample(
				start,
				len,
				MDIOSymbol(MDIOSymbol::TYPE_PHYADDR, addr)));
			i += 5;

			char tmp[32];
			snprintf(tmp, sizeof(tmp), "%02x", addr);
			pack->m_headers["PHY"] = tmp;

			//Next 5 bits are reg address
			if(i+5 >= dmdio.size())
			{
				delete pack;
				break;
			}
			addr = 0;
			start = dmdio[i].m_offset;
			for(size_t j=0; j<5; j++)
			{
				len = (dmdio[i+j].m_offset - start) + dmdio[i+j].m_duration;
				addr <<= 1;
				if(dmdio[i+j].m_sample)
					addr |= 1;
			}
			cap->m_samples.push_back(MDIOSample(
				start,
				len,
				MDIOSymbol(MDIOSymbol::TYPE_REGADDR, addr)));
			i += 5;

			snprintf(tmp, sizeof(tmp), "%02x", addr);
			pack->m_headers["Reg"] = tmp;

			//Next 2 bits are bus turnaround
			if(i+2 >= dmdio.size())
				break;
			cap->m_samples.push_back(MDIOSample(
				dmdio[i].m_offset,
				(dmdio[i+1].m_offset - dmdio[i].m_offset) + dmdio[i+1].m_duration,
				MDIOSymbol(MDIOSymbol::TYPE_TURN, 0)));
			i += 2;

			//Next 16 bits are frame data
			if(i+16 >= dmdio.size())
			{
				delete pack;
				break;
			}
			uint16_t value = 0;
			start = dmdio[i].m_offset;
			for(size_t j=0; j<16; j++)
			{
				len = (dmdio[i+j].m_offset - start) + dmdio[i+j].m_duration;
				value <<= 1;
				if(dmdio[i+j].m_sample)
					value |= 1;
			}
			cap->m_samples.push_back(MDIOSample(
				start,
				len,
				MDIOSymbol(MDIOSymbol::TYPE_DATA, value)));
			i += 16;

			snprintf(tmp, sizeof(tmp), "%04x", value);
			pack->m_headers["Value"] = tmp;

			//Add extra information to the decode if it's a known register
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
			cap->m_samples.push_back(MDIOSample(
				dmdio[i].m_offset,
				(dmdio[i+1].m_offset - dmdio[i].m_offset) + dmdio[i+1].m_duration,
				MDIOSymbol(MDIOSymbol::TYPE_ERROR, 0)));
		}
	}

	SetData(cap);
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
