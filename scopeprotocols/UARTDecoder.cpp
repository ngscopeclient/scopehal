/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2017 Andrew D. Zonenberg                                                                          *
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
#include "../scopehal/ChannelRenderer.h"
#include "../scopehal/TextRenderer.h"
#include "../scopehal/AsciiRenderer.h"
#include "UARTDecoder.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

UARTDecoder::UARTDecoder(
	std::string hwname, std::string color)
	: ProtocolDecoder(hwname, OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color)
{
	//Set up channels
	m_signalNames.push_back("din");
	m_channels.push_back(NULL);

	m_baudname = "Baud rate";
	m_parameters[m_baudname] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_INT);
	m_parameters[m_baudname].SetIntVal(115200);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool UARTDecoder::NeedsConfig()
{
	//baud rate has to be set
	return true;
}

ChannelRenderer* UARTDecoder::CreateRenderer()
{
	return new AsciiRenderer(this);
}

bool UARTDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) && (channel->GetWidth() == 1) )
		return true;
	return false;
}

std::string UARTDecoder::GetProtocolName()
{
	return "UART";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void UARTDecoder::Refresh()
{
	//Get the input data
	if(m_channels[0] == NULL)
	{
		SetData(NULL);
		return;
	}
	DigitalCapture* din = dynamic_cast<DigitalCapture*>(m_channels[0]->GetData());
	if(din == NULL)
	{
		SetData(NULL);
		return;
	}

	//Get the bit period
	float bit_period = 1.0f / m_parameters[m_baudname].GetFloatVal();
	bit_period *= 1E12;
	int64_t ibitper = bit_period;
	int64_t scaledbitper = ibitper / din->m_timescale;

	//UART processing
	AsciiCapture* cap = new AsciiCapture;
	m_timescale = m_channels[0]->m_timescale;
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
	}

	SetData(cap);
}
