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
#include "TachometerFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

TachometerFilter::TachometerFilter(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MISC)
{
	m_yAxisUnit = Unit(Unit::UNIT_RPM);

	//Set up channels
	CreateInput("din");

	m_midpoint = 0.5;
	m_range = 1;

	m_ticksname = "Pulses per revolution";
	m_parameters[m_ticksname] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_ticksname].SetIntVal(1);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool TachometerFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void TachometerFilter::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "Tachometer(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string TachometerFilter::GetProtocolName()
{
	return "Tachometer";
}

bool TachometerFilter::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool TachometerFilter::NeedsConfig()
{
	return true;
}

double TachometerFilter::GetVoltageRange()
{
	return m_range;
}

double TachometerFilter::GetOffset()
{
	return -m_midpoint;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void TachometerFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}
	auto din = GetAnalogInputWaveform(0);

	//Find average voltage of the waveform and use that as the zero crossing
	float midpoint = GetAvgVoltage(din);

	//Timestamps of the edges
	vector<int64_t> edges;
	FindZeroCrossings(din, midpoint, edges);
	if(edges.size() < 2)
	{
		SetData(NULL, 0);
		return;
	}

	//Create the output
	auto cap = new AnalogWaveform;

	int64_t pulses_per_rev = m_parameters[m_ticksname].GetIntVal();
	float pulses_to_rpm = 60.0f / pulses_per_rev;

	double rmin = FLT_MAX;
	double rmax = 0;
	size_t elen = edges.size();
	for(size_t i=0; i < (elen - 2); i+= 2)
	{
		//measure from edge to 2 edges later, since we find all zero crossings regardless of polarity
		int64_t start = edges[i];
		int64_t end = edges[i+2];

		int64_t delta = end - start;
		double freq = FS_PER_SECOND / delta;
		double rpm = freq * pulses_to_rpm;

		cap->m_offsets.push_back(start);
		cap->m_durations.push_back(delta);
		cap->m_samples.push_back(rpm);

		rmin = min(rmin, rpm);
		rmax = max(rmax, rpm);
	}

	m_range = rmax - rmin;
	m_midpoint = rmin + m_range/2;

	//minimum scale
	if(m_range < 0.001*m_midpoint)
		m_range = 0.001*m_midpoint;

	SetData(cap, 0);

	//Copy start time etc from the input. Timestamps are in femtoseconds.
	cap->m_timescale = 1;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
}
