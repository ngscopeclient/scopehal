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
	@brief Implementation of IBM8b10bDecoder
 */

#include "../scopehal/scopehal.h"
#include "../scopehal/ChannelRenderer.h"
#include "../scopehal/TextRenderer.h"
#include "IBM8b10bRenderer.h"
#include "IBM8b10bDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

IBM8b10bDecoder::IBM8b10bDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_SERIAL)
{
	//Set up channels
	m_signalNames.push_back("data");
	m_channels.push_back(NULL);

	m_signalNames.push_back("clk");
	m_channels.push_back(NULL);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool IBM8b10bDecoder::NeedsConfig()
{
	//baud rate has to be set
	return true;
}

ChannelRenderer* IBM8b10bDecoder::CreateRenderer()
{
	return new IBM8b10bRenderer(this);
}

bool IBM8b10bDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) && (channel->GetWidth() == 1) )
		return true;
	if( (i == 1) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) && (channel->GetWidth() == 1) )
		return true;
	return false;
}

string IBM8b10bDecoder::GetProtocolName()
{
	return "8b/10b (IBM)";
}

void IBM8b10bDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "8b10b(%s)", m_channels[0]->m_displayname.c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void IBM8b10bDecoder::Refresh()
{
	//Get the input data
	if( (m_channels[0] == NULL) || (m_channels[1] == NULL) )
	{
		SetData(NULL);
		return;
	}
	DigitalCapture* din = dynamic_cast<DigitalCapture*>(m_channels[0]->GetData());
	DigitalCapture* clkin = dynamic_cast<DigitalCapture*>(m_channels[1]->GetData());
	if( (din == NULL) || (clkin == NULL) )
	{
		SetData(NULL);
		return;
	}

	//Create the capture
	IBM8b10bCapture* cap = new IBM8b10bCapture;
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startPicoseconds = din->m_startPicoseconds;

	//Create a few dummy test samples
	cap->m_samples.push_back(IBM8b10bSample(200, 100, IBM8b10bSymbol(true, 0xff)));
	cap->m_samples.push_back(IBM8b10bSample(100, 100, IBM8b10bSymbol(false, 0x55)));

	/*
	//Get the bit period
	float bit_period = 1.0f / m_parameters[m_baudname].GetFloatVal();
	bit_period *= 1E12;
	int64_t ibitper = bit_period;
	int64_t scaledbitper = ibitper / din->m_timescale;

	//IBM8b10b processing
	AsciiCapture* cap = new AsciiCapture;
	cap->m_timescale = din->m_timescale;

	//Time-domain processing to reflect potentially variable sampling rate for RLE captures
	int64_t next_value = 0;
	size_t isample = 0;
	while(isample < din->m_samples.size())
	{
		//Wait for signal to go high (idle state)
		while( (isample < din->m_samples.size()) && !din->m_samples[isample].m_sample)
			isample ++;
		if(isample >= din->m_samples.size())
			break;

		//Wait for a falling edge (start bit)
		while( (isample < din->m_samples.size()) && din->m_samples[isample].m_sample)
			isample ++;
		if(isample >= din->m_samples.size())
			break;

		//Time of the start bit
		int64_t tstart = din->m_samples[isample].m_offset;

		//The next data bit should be measured 1.5 bit periods after the falling edge
		next_value = tstart + scaledbitper + scaledbitper/2;

		//Read eight data bits
		unsigned char dval = 0;
		for(int ibit=0; ibit<8; ibit++)
		{
			//Find the sample of interest
			while( (isample < din->m_samples.size()) && ((din->m_samples[isample].m_offset + din->m_samples[isample].m_duration) < next_value))
				isample ++;
			if(isample >= din->m_samples.size())
				break;

			//Got the sample
			dval = (dval >> 1) | (din->m_samples[isample].m_sample ? 0x80 : 0);

			//Go on to the next bit
			next_value += scaledbitper;
		}

		//If we ran out of space before we hit the end of the buffer, abort
		if(isample >= din->m_samples.size())
			break;

		//All good, read the stop bit
		while( (isample < din->m_samples.size()) && ((din->m_samples[isample].m_offset + din->m_samples[isample].m_duration) < next_value))
			isample ++;
		if(isample >= din->m_samples.size())
			break;

		//Save the sample
		int64_t tend = next_value + (scaledbitper/2);
		cap->m_samples.push_back(AsciiSample(
			tstart,
			tend-tstart,
			(char)dval));
	}*/

	SetData(cap);
}
