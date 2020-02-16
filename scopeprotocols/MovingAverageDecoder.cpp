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

#include "scopeprotocols.h"
#include "../scopehal/AnalogRenderer.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

MovingAverageDecoder::MovingAverageDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MATH)
{
	//Set up channels
	m_signalNames.push_back("din");
	m_channels.push_back(NULL);

	m_depthname = "Depth";
	m_parameters[m_depthname] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_INT);
	m_parameters[m_depthname].SetFloatVal(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

ChannelRenderer* MovingAverageDecoder::CreateRenderer()
{
	return new AnalogRenderer(this);
}

bool MovingAverageDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

double MovingAverageDecoder::GetVoltageRange()
{
	return m_channels[0]->GetVoltageRange();
}

double MovingAverageDecoder::GetOffset()
{
	return m_channels[0]->GetOffset();
}

string MovingAverageDecoder::GetProtocolName()
{
	return "Moving average";
}

bool MovingAverageDecoder::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool MovingAverageDecoder::NeedsConfig()
{
	//we need the depth to be specified, duh
	return true;
}

void MovingAverageDecoder::SetDefaultName()
{
	char hwname[256];
	int depth = m_parameters[m_depthname].GetIntVal();
	snprintf(hwname, sizeof(hwname), "MovingAvg(%s, %d)", m_channels[0]->m_displayname.c_str(), depth);
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void MovingAverageDecoder::Refresh()
{
	//Get the input data
	if(m_channels[0] == NULL)
	{
		SetData(NULL);
		return;
	}
	AnalogCapture* din = dynamic_cast<AnalogCapture*>(m_channels[0]->GetData());

	//We need meaningful data
	if(din->GetDepth() == 0)
	{
		SetData(NULL);
		return;
	}

	size_t depth = m_parameters[m_depthname].GetIntVal();

	m_yAxisUnit = m_channels[0]->GetYAxisUnits();

	//Do the average
	AnalogCapture* cap = new AnalogCapture;

	for(size_t i=0; i<din->m_samples.size(); i++)
	{
		float v = 0;
		size_t navg = 0;
		for(size_t j=0; j<depth; j++)
		{
			if(j > i)
				break;

			v += din->m_samples[i-j].m_sample;
			navg ++;
		}
		v /= navg;

		cap->m_samples.push_back(AnalogSample(
			din->m_samples[i].m_offset, din->m_samples[i].m_duration, v));
	}
	SetData(cap);

	//Copy our time scales from the input
	cap->m_timescale = din->m_timescale;
}
