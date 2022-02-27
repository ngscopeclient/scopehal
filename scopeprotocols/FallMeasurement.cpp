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

#include "scopeprotocols.h"
#include "FallMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

FallMeasurement::FallMeasurement(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MEASUREMENT)
{
	//Set up channels
	CreateInput("din");

	m_startname = "Start Fraction";
	m_parameters[m_startname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_PERCENT));
	m_parameters[m_startname].SetFloatVal(0.8);

	m_endname = "End Fraction";
	m_parameters[m_endname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_PERCENT));
	m_parameters[m_endname].SetFloatVal(0.2);

	SetYAxisUnits(Unit(Unit::UNIT_FS), 0);

	m_midpoint = 0;
	m_range = 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool FallMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void FallMeasurement::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "Fall(%s, %s, %s)",
		GetInputDisplayName(0).c_str(),
		m_parameters[m_startname].ToString().c_str(),
		m_parameters[m_endname].ToString().c_str()
		);
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string FallMeasurement::GetProtocolName()
{
	return "Fall";
}

bool FallMeasurement::NeedsConfig()
{
	return true;
}

float FallMeasurement::GetVoltageRange(size_t /*stream*/)
{
	return m_range;
}

float FallMeasurement::GetOffset(size_t /*stream*/)
{
	return -m_midpoint;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void FallMeasurement::Refresh()
{
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din = GetAnalogInputWaveform(0);
	size_t len = din->m_samples.size();

	//Get the base/top (we use these for calculating percentages)
	float base = GetBaseVoltage(din);
	float top = GetTopVoltage(din);

	//Find the actual levels we use for our time gate
	float delta = top - base;
	float vstart = base + m_parameters[m_startname].GetFloatVal()*delta;
	float vend = base + m_parameters[m_endname].GetFloatVal()*delta;

	//Create the output
	auto cap = new AnalogWaveform;

	float last = -1e20;
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
			if( (cur < vstart) && (last >= vstart) )
			{
				tedge = tnow - din->m_timescale + InterpolateTime(din, i-1, vstart)*din->m_timescale;
				state = 1;
			}
		}

		//Find end of edge
		else if(state == 1)
		{
			if( (cur < vend) && (last >= vend) )
			{
				double dt = InterpolateTime(din, i-1, vend)*din->m_timescale + tnow - din->m_timescale - tedge;

				cap->m_offsets.push_back(tlast);
				cap->m_durations.push_back(tnow - tlast);
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

	SetData(cap, 0);

	//Copy start time etc from the input. Timestamps are in femtoseconds.
	cap->m_timescale = 1;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
}
