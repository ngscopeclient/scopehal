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

#include "scopeprotocols.h"
#include "RiseMeasurementDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

RiseMeasurementDecoder::RiseMeasurementDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MEASUREMENT)
{
	//Set up channels
	m_signalNames.push_back("din");
	m_channels.push_back(NULL);

	m_startname = "Start Fraction";
	m_parameters[m_startname] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_FLOAT);
	m_parameters[m_startname].SetFloatVal(0.2);

	m_endname = "End Fraction";
	m_parameters[m_endname] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_FLOAT);
	m_parameters[m_endname].SetFloatVal(0.8);

	m_yAxisUnit = Unit(Unit::UNIT_PS);

	m_midpoint = 0;
	m_range = 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool RiseMeasurementDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void RiseMeasurementDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "Rise(%s, %d, %d)",
		m_channels[0]->m_displayname.c_str(),
		static_cast<int>(m_parameters[m_startname].GetFloatVal() * 100),
		static_cast<int>(m_parameters[m_endname].GetFloatVal() * 100)
		);
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string RiseMeasurementDecoder::GetProtocolName()
{
	return "Rise";
}

bool RiseMeasurementDecoder::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool RiseMeasurementDecoder::NeedsConfig()
{
	return true;
}

double RiseMeasurementDecoder::GetVoltageRange()
{
	return m_range;
}

double RiseMeasurementDecoder::GetOffset()
{
	return -m_midpoint;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void RiseMeasurementDecoder::Refresh()
{
	//Get the input data
	if(m_channels[0] == NULL)
	{
		SetData(NULL);
		return;
	}
	auto din = dynamic_cast<AnalogWaveform*>(m_channels[0]->GetData());
	if(din == NULL)
	{
		SetData(NULL);
		return;
	}

	//We need meaningful data
	size_t len = din->m_samples.size();
	if(len == 0)
	{
		SetData(NULL);
		return;
	}

	//Get the base/top (we use these for calculating percentages)
	float base = GetBaseVoltage(din);
	float top = GetTopVoltage(din);

	//Find the actual levels we use for our time gate
	float delta = top - base;
	float vstart = base + m_parameters[m_startname].GetFloatVal()*delta;
	float vend = base + m_parameters[m_endname].GetFloatVal()*delta;

	//Create the output
	auto cap = new AnalogWaveform;

	float last = 1e20;
	double tedge = 0;
	float fmax = -1e20;
	float fmin =  1e20;

	int state = 0;
	int64_t tlast = 0;

	//LogDebug("vstart = %.3f, vend = %.3f\n", vstart, vend);
	for(size_t i=0; i < len; i++)
	{
		float cur = din->m_samples[i];
		int64_t tnow = din->m_offsets[i] * din->m_timescale;

		//Find start of edge
		if(state == 0)
		{
			if( (cur > vstart) && (last <= vstart) )
			{
				tedge = tnow - din->m_timescale + InterpolateTime(din, i-1, vstart)*din->m_timescale;
				state = 1;
			}
		}

		//Find end of edge
		else if(state == 1)
		{
			if( (cur > vend) && (last <= vend) )
			{
				double dt = InterpolateTime(din, i-1, vend)*din->m_timescale + tnow - din->m_timescale - tedge;

				cap->m_offsets.push_back(tlast);
				cap->m_durations.push_back(tnow-tlast);
				cap->m_samples.push_back(dt);
				tlast = tnow;

				if(dt < fmin)
					fmin = dt;
				if(dt > fmax)
					fmax = dt;

				state = 0;
			}
		}

		last = cur;
	}

	m_range = fmax - fmin;
	m_midpoint = (fmax + fmin) / 2;

	//minimum scale
	if(m_range < 0.001*m_midpoint)
		m_range = 0.001*m_midpoint;
	if(m_range < 200)
		m_range = 200;

	SetData(cap);

	//Copy start time etc from the input. Timestamps are in picoseconds.
	cap->m_timescale = 1;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startPicoseconds = din->m_startPicoseconds;
}
