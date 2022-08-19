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
	@brief Implementation of UARTDecoder
 */

#include "../scopehal/scopehal.h"
#include "UARTDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

UARTDecoder::UARTDecoder(const string& color)
	: PacketDecoder(color, CAT_BUS)
{
	//Set up channels
	CreateInput("din");

	m_baudname = "Baud rate";
	m_parameters[m_baudname] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_BITRATE));
	m_parameters[m_baudname].SetIntVal(115200);
}

UARTDecoder::~UARTDecoder()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

vector<string> UARTDecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("Length");
	ret.push_back("ASCII");
	return ret;
}

bool UARTDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;
	return false;
}

string UARTDecoder::GetProtocolName()
{
	return "UART";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void UARTDecoder::Refresh()
{
	ClearPackets();

	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din = GetDigitalInputWaveform(0);

	//Get the bit period
	float bit_period = FS_PER_SECOND / m_parameters[m_baudname].GetFloatVal();
	int64_t ibitper = bit_period;
	int64_t scaledbitper = ibitper / din->m_timescale;

	//UART processing
	auto cap = new ASCIIWaveform(m_displaycolor);
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;

	//Time-domain processing to reflect potentially variable sampling rate for RLE captures
	int64_t next_value = 0;
	size_t isample = 0;
	int64_t tlast = 0;
	Packet* pack = NULL;
	size_t len = din->m_samples.size();
	while(isample < len)
	{
		//Wait for signal to go high (idle state)
		while( (isample < len) && !din->m_samples[isample])
			isample ++;
		if(isample >= len)
			break;

		//Wait for a falling edge (start bit)
		while( (isample < len) && din->m_samples[isample])
			isample ++;
		if(isample >= len)
			break;

		//Time of the start bit
		int64_t tstart = din->m_offsets[isample];

		//The next data bit should be measured 1.5 bit periods after the falling edge
		next_value = tstart + scaledbitper + scaledbitper/2;

		//Read eight data bits
		unsigned char dval = 0;
		for(int ibit=0; ibit<8; ibit++)
		{
			//Find the sample of interest
			while( (isample < len) && ((din->m_offsets[isample] + din->m_durations[isample]) < next_value))
				isample ++;
			if(isample >= len)
				break;

			//Got the sample
			dval = (dval >> 1) | (din->m_samples[isample] ? 0x80 : 0);

			//Go on to the next bit
			next_value += scaledbitper;
		}

		//If we ran out of space before we hit the end of the buffer, abort
		if(isample >= len)
			break;

		//All good, read the stop bit
		while( (isample < len) && ((din->m_offsets[isample] + din->m_durations[isample]) < next_value))
			isample ++;
		if(isample >= len)
			break;

		//Save the sample
		int64_t tend = next_value + (scaledbitper/2);
		cap->m_offsets.push_back(tstart);
		cap->m_durations.push_back(tend-tstart);
		cap->m_samples.push_back(dval);

		//If the last packet was more than 3 byte times ago, start a new one
		if(pack != NULL)
		{
			int64_t delta = tstart - tlast;
			if(delta > 30 * scaledbitper)
			{
				pack->m_len = (tend * din->m_timescale) - pack->m_offset;
				FinishPacket(pack);
				pack = NULL;
			}
		}

		//If we don't have a packet yet, start one
		if(pack == NULL)
		{
			pack = new Packet;
			pack->m_offset = tstart * din->m_timescale;
		}

		//Append to the existing packet
		pack->m_data.push_back(dval);
		tlast = tstart;
	}

	//If we have a packet in progress, add it
	if(pack)
	{
		pack->m_len = (din->m_offsets[len-1] * din->m_timescale) - pack->m_offset;
		FinishPacket(pack);
	}

	SetData(cap, 0);
}

void UARTDecoder::FinishPacket(Packet* pack)
{
	//length header
	char tmp[128];
	snprintf(tmp, sizeof(tmp), "%zu", pack->m_data.size());
	pack->m_headers["Length"] = tmp;

	//ascii packet contents
	string s;
	for(auto b : pack->m_data)
	{
		if(isprint(b))
			s += (char)b;
		else
			s += ".";
	}
	pack->m_headers["ASCII"] = s;

	m_packets.push_back(pack);
}

Gdk::Color ASCIIWaveform::GetColor(size_t /*i*/)
{
	return Gdk::Color(m_color);
}

string ASCIIWaveform::GetText(size_t i)
{
	char c = m_samples[i];
	char sbuf[16] = {0};
	if(isprint(c))
		sbuf[0] = c;
	else if(c == '\r')		//special case common non-printable chars
		return "\\r";
	else if(c == '\n')
		return "\\n";
	else if(c == '\b')
		return "\\b";
	else
		snprintf(sbuf, sizeof(sbuf), "\\x%02x", 0xFF & c);
	return sbuf;
}
