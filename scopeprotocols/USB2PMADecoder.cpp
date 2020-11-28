
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
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

USB2PMADecoder::USB2PMADecoder(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_SERIAL)
{
	//Set up channels
	CreateInput("D+");
	CreateInput("D-");

	//TODO: make this an enum/bool
	m_speedname = "Full Speed";
	m_parameters[m_speedname] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_speedname].SetIntVal(1);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool USB2PMADecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 2) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void USB2PMADecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "USB2PMA(%s,%s)",
		GetInputDisplayName(0).c_str(),
		GetInputDisplayName(1).c_str());
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
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din_p = GetAnalogInputWaveform(0);
	auto din_n = GetAnalogInputWaveform(1);
	size_t len = min(din_p->m_samples.size(), din_n->m_samples.size());

	//Figure out our speed so we know what's going on
	int speed = m_parameters[m_speedname].GetIntVal();

	//Figure out the line state for each input (no clock recovery yet)
	auto cap = new USB2PMAWaveform;
	for(size_t i=0; i<len; i++)
	{
		bool bp = (din_p->m_samples[i] > 0.4);
		bool bn = (din_n->m_samples[i] > 0.4);
		float vdiff = din_p->m_samples[i] - din_n->m_samples[i];

		USB2PMASymbol::SegmentType type = USB2PMASymbol::TYPE_SE1;
		if(fabs(vdiff) > 0.4)
		{
			if(speed == 1)
			{
				if(vdiff > 0)
					type = USB2PMASymbol::TYPE_J;
				else
					type = USB2PMASymbol::TYPE_K;
			}
			else
			{
				if(vdiff > 0)
					type = USB2PMASymbol::TYPE_K;
				else
					type = USB2PMASymbol::TYPE_J;
			}
		}
		else if(bp && bn)
			type = USB2PMASymbol::TYPE_SE1;
		else
			type = USB2PMASymbol::TYPE_SE0;

		//First sample goes as-is
		if(cap->m_samples.empty())
		{
			cap->m_offsets.push_back(din_p->m_offsets[i]);
			cap->m_durations.push_back(din_p->m_durations[i]);
			cap->m_samples.push_back(type);
			continue;
		}

		//Type match? Extend the existing sample
		size_t iold = cap->m_samples.size()-1;
		auto oldtype = cap->m_samples[iold];
		if(oldtype == type)
		{
			cap->m_durations[iold] += din_p->m_durations[i];
			continue;
		}

		//Ignore SE0/SE1 states during transitions.
		int64_t last_fs = cap->m_durations[iold] * din_p->m_timescale;
		if(
			( (oldtype == USB2PMASymbol::TYPE_SE0) || (oldtype == USB2PMASymbol::TYPE_SE1) ) &&
			(last_fs < 100000000))
		{
			cap->m_samples[iold].m_type = type;
			cap->m_durations[iold] += din_p->m_durations[i];
			continue;
		}

		//Not a match. Add a new sample.
		cap->m_offsets.push_back(din_p->m_offsets[i]);
		cap->m_durations.push_back(din_p->m_durations[i]);
		cap->m_samples.push_back(type);
	}

	SetData(cap, 0);

	//Copy our time scales from the input
	//Use the first trace's timestamp as our start time if they differ
	cap->m_timescale = din_p->m_timescale;
	cap->m_startTimestamp = din_p->m_startTimestamp;
	cap->m_startFemtoseconds = din_p->m_startFemtoseconds;
}

Gdk::Color USB2PMADecoder::GetColor(int i)
{
	auto data = dynamic_cast<USB2PMAWaveform*>(GetData(0));
	if(data == NULL)
		return m_standardColors[COLOR_ERROR];
	if(i >= (int)data->m_samples.size())
		return m_standardColors[COLOR_ERROR];

	//TODO: have a set of standard colors we use everywhere?
	auto sample = data->m_samples[i];
	switch(sample.m_type)
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
	auto data = dynamic_cast<USB2PMAWaveform*>(GetData(0));
	if(data == NULL)
		return "";
	if(i >= (int)data->m_samples.size())
		return "";

	auto sample = data->m_samples[i];
	switch(sample.m_type)
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
