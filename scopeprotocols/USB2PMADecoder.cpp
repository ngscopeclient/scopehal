
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
#include "USB2PMADecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

USB2PMADecoder::USB2PMADecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_SERIAL)
{
	//Set up channels
	m_signalNames.push_back("D+");
	m_signalNames.push_back("D-");
	m_channels.push_back(NULL);
	m_channels.push_back(NULL);

	//TODO: make this an enum
	m_speedname = "Full Speed";
	m_parameters[m_speedname] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_INT);
	m_parameters[m_speedname].SetIntVal(1);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool USB2PMADecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	if( (i == 1) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void USB2PMADecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "USB2PMA(%s,%s)",
		m_channels[0]->m_displayname.c_str(), m_channels[1]->m_displayname.c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string USB2PMADecoder::GetProtocolName()
{
	return "USB 1.x/2.0 PMA";
}

bool USB2PMADecoder::IsOverlay()
{
	return true;
}

bool USB2PMADecoder::NeedsConfig()
{
	return true;
}

double USB2PMADecoder::GetVoltageRange()
{
	return 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void USB2PMADecoder::Refresh()
{
	//Get the input data
	if( (m_channels[0] == NULL) || (m_channels[1] == NULL) )
	{
		SetData(NULL);
		return;
	}
	AnalogCapture* din_p = dynamic_cast<AnalogCapture*>(m_channels[0]->GetData());
	AnalogCapture* din_n = dynamic_cast<AnalogCapture*>(m_channels[1]->GetData());
	if( (din_p == NULL) || (din_n == NULL) )
	{
		SetData(NULL);
		return;
	}

	//We need meaningful data
	if(din_p->GetDepth() == 0)
	{
		SetData(NULL);
		return;
	}

	//Figure out our speed so we know what's going on
	int speed = m_parameters[m_speedname].GetIntVal();

	//Figure out the line state for each input (no clock recovery yet)
	USB2PMACapture* cap = new USB2PMACapture;
	for(size_t i=0; i<din_p->m_samples.size(); i++)
	{
		const AnalogSample& sin_p = din_p->m_samples[i];
		const AnalogSample& sin_n = din_n->m_samples[i];

		//TODO: handle this better.
		bool bp = (sin_p.m_sample > 0.4);
		bool bn = (sin_n.m_sample > 0.4);

		USB2PMASymbol::SegmentType type = USB2PMASymbol::TYPE_SE1;
		if(bp && bn)
			type = USB2PMASymbol::TYPE_SE1;
		else if(!bp && !bn)
			type = USB2PMASymbol::TYPE_SE0;
		else
		{
			if(speed == 1)
			{
				if(bp && !bn)
					type = USB2PMASymbol::TYPE_J;
				else
					type = USB2PMASymbol::TYPE_K;
			}
			else
			{
				if(bp && !bn)
					type = USB2PMASymbol::TYPE_K;
				else
					type = USB2PMASymbol::TYPE_J;
			}
		}

		//First sample goes as-is
		if(cap->m_samples.empty())
		{
			cap->m_samples.push_back(USBLineSample(
				sin_p.m_offset,
				sin_p.m_duration,
				type));
			continue;
		}

		//Type match? Extend the existing sample
		USBLineSample& oldsample = cap->m_samples[cap->m_samples.size()-1];
		USB2PMASymbol::SegmentType &oldtype = oldsample.m_sample.m_type;
		if(oldtype == type)
		{
			oldsample.m_duration += sin_p.m_duration;
			continue;
		}

		//Ignore SE0/SE1 states during transitions.
		int64_t last_ps = oldsample.m_duration * din_p->m_timescale;
		if(
			( (oldtype == USB2PMASymbol::TYPE_SE0) || (oldtype == USB2PMASymbol::TYPE_SE1) ) &&
			(last_ps < 100000))
		{
			oldsample.m_sample.m_type = type;
			oldsample.m_duration += sin_p.m_duration;
			continue;
		}

		//Not a match. Add a new sample.
		cap->m_samples.push_back(USBLineSample(
			sin_p.m_offset,
			sin_p.m_duration,
			type));
	}

	SetData(cap);

	//Copy our time scales from the input
	//Use the first trace's timestamp as our start time if they differ
	cap->m_timescale = din_p->m_timescale;
	cap->m_startTimestamp = din_p->m_startTimestamp;
	cap->m_startPicoseconds = din_p->m_startPicoseconds;
}

Gdk::Color USB2PMADecoder::GetColor(int i)
{
	USB2PMACapture* data = dynamic_cast<USB2PMACapture*>(GetData());
	if(data == NULL)
		return m_standardColors[COLOR_ERROR];
	if(i >= (int)data->m_samples.size())
		return m_standardColors[COLOR_ERROR];

	//TODO: have a set of standard colors we use everywhere?

	auto sample = data->m_samples[i];
	switch(sample.m_sample.m_type)
	{
		case USB2PMASymbol::TYPE_J:
		case USB2PMASymbol::TYPE_K:
			return m_standardColors[COLOR_DATA];

		case USB2PMASymbol::TYPE_SE0:
			return m_standardColors[COLOR_PREAMBLE];

		//invalid state, should never happen
		case USB2PMASymbol::TYPE_SE1:
		default:
			return m_standardColors[COLOR_ERROR];
	}
}

string USB2PMADecoder::GetText(int i)
{
	USB2PMACapture* data = dynamic_cast<USB2PMACapture*>(GetData());
	if(data == NULL)
		return "";
	if(i >= (int)data->m_samples.size())
		return "";

	auto sample = data->m_samples[i];
	switch(sample.m_sample.m_type)
	{
		case USB2PMASymbol::TYPE_J:
			return "J";
		case USB2PMASymbol::TYPE_K:
			return "K";
		case USB2PMASymbol::TYPE_SE0:
			return "SE0";
		case USB2PMASymbol::TYPE_SE1:
			return "SE1";
	}

	return "";
}
