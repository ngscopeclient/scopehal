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
#include "DutyCycleMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DutyCycleMeasurement::DutyCycleMeasurement(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MEASUREMENT)
{
	m_yAxisUnit = Unit(Unit::UNIT_PERCENT);

	//Set up channels
	CreateInput("din");

	m_midpoint = 0.5;
	m_range = 1;

	m_rmin = 0;
	m_rmax = 0.001;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DutyCycleMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void DutyCycleMeasurement::ClearSweeps()
{
	m_midpoint = 0.5;
	m_range = 1;

	m_rmin = 0;
	m_rmax = 0.001;
}

void DutyCycleMeasurement::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "DutyCycle(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string DutyCycleMeasurement::GetProtocolName()
{
	return "Duty Cycle";
}

bool DutyCycleMeasurement::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool DutyCycleMeasurement::NeedsConfig()
{
	//automatic configuration
	return false;
}

double DutyCycleMeasurement::GetVoltageRange()
{
	return m_range;
}

double DutyCycleMeasurement::GetOffset()
{
	return -m_midpoint;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DutyCycleMeasurement::Refresh()
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

	//Figure out edge polarity
	bool initial_polarity = (din->m_samples[0] > midpoint);

	size_t elen = edges.size();
	for(size_t i=0; i < (elen - 2); i+= 2)
	{
		//measure from edge to 2 edges later, since we find all zero crossings regardless of polarity
		int64_t start = edges[i];
		int64_t mid = edges[i+1];
		int64_t end = edges[i+2];

		double t1 = mid-start;
		double t2 = end-mid;
		double total = t1+t2;

		double duty;

		//T1 is high time
		if(!initial_polarity)
			duty = t1/total;
		else
			duty = t2/total;

		cap->m_offsets.push_back(start);
		cap->m_durations.push_back(total);
		cap->m_samples.push_back(duty);

		m_rmin = min(m_rmin, duty);
		m_rmax = max(m_rmax, duty);
	}

	m_range = m_rmax - m_rmin;
	m_midpoint = m_rmin + m_range/2;

	SetData(cap, 0);

	//Copy start time etc from the input. Timestamps are in femtoseconds.
	cap->m_timescale = 1;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
}
