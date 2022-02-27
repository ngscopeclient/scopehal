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

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

TIEMeasurement::TIEMeasurement(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_CLOCK)
	, m_threshname("Threshold")
	, m_skipname("Skip Start")
{
	SetYAxisUnits(Unit(Unit::UNIT_FS), 0);

	//Set up channels
	CreateInput("Clock");
	CreateInput("Golden");

	ClearSweeps();

	m_parameters[m_threshname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_threshname].SetFloatVal(0);

	m_parameters[m_skipname] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));
	m_parameters[m_skipname].SetIntVal(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool TIEMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) )//allow digital clocks
		return true;
	if( (i == 1) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void TIEMeasurement::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "TIE(%s, %s)",
		GetInputDisplayName(0).c_str(),
		GetInputDisplayName(1).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string TIEMeasurement::GetProtocolName()
{
	return "Clock Jitter (TIE)";
}

bool TIEMeasurement::NeedsConfig()
{
	//we have more than one input
	return true;
}

float TIEMeasurement::GetVoltageRange(size_t /*stream*/)
{
	return m_range;
}

float TIEMeasurement::GetOffset(size_t /*stream*/)
{
	return m_offset;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void TIEMeasurement::ClearSweeps()
{
	m_range = 1;
	m_offset = 0;
	m_min = FLT_MAX;
	m_max = -FLT_MAX;
}

void TIEMeasurement::Refresh()
{
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto clk_analog = GetAnalogInputWaveform(0);
	auto clk_digital = GetDigitalInputWaveform(0);
	WaveformBase* clk = GetInputWaveform(0);
	auto golden = GetDigitalInputWaveform(1);
	size_t len = min(clk->m_offsets.size(), golden->m_offsets.size());

	//Create the output
	auto cap = new AnalogWaveform;

	//Timestamps of the edges
	vector<int64_t> edges;
	if(clk_analog)
		FindZeroCrossings(clk_analog, m_parameters[m_threshname].GetFloatVal(), edges);
	else
		FindZeroCrossings(clk_digital, edges);

	//Ignore edges before things have stabilized
	int64_t skip_time = m_parameters[m_skipname].GetIntVal();

	int64_t vmin = FS_PER_SECOND;
	int64_t vmax = -FS_PER_SECOND;

	//For each input clock edge, find the closest recovered clock edge
	size_t iedge = 0;
	size_t tlast = 0;
	for(auto atime : edges)
	{
		if(iedge >= len)
			break;

		int64_t prev_edge = golden->m_offsets[iedge] * golden->m_timescale;
		int64_t next_edge = prev_edge;
		size_t jedge = iedge;

		bool hit = false;

		//Look for a pair of edges bracketing our edge
		while(true)
		{
			prev_edge = next_edge;
			next_edge = golden->m_offsets[jedge] * golden->m_timescale;

			//First golden edge is after this signal edge
			if(prev_edge > atime)
				break;

			//Bracketed
			if( (prev_edge < atime) && (next_edge > atime) )
			{
				hit = true;
				break;
			}

			//No, keep looking
			jedge ++;

			//End of capture
			if(jedge >= len)
				break;
		}

		//No interval error possible without a reference clock edge.
		if(!hit)
			continue;

		//Hit! We're bracketed. Start the next search from this edge
		iedge = jedge;

		//Since the CDR filter adds a 90 degree phase offset for sampling in the middle of the data eye,
		//we need to use the *midpoint* of the golden clock cycle as the nominal position of the clock
		//edge for TIE measurements.
		int64_t golden_period = next_edge - prev_edge;
		int64_t golden_center = prev_edge + golden_period/2;
		int64_t tie = atime - golden_center;

		//Ignore edges before things have stabilized
		if(prev_edge < skip_time)
		{}

		else
		{
			//Update the last sample
			size_t end = cap->m_durations.size();
			if(end)
				cap->m_durations[end-1] = atime - tlast;

			vmax = max(vmax, tie);
			vmin = min(vmin, tie);

			cap->m_offsets.push_back(golden_center);
			cap->m_durations.push_back(0);
			cap->m_samples.push_back(tie);
		}

		tlast = golden_center;
	}

	SetData(cap, 0);

	//Copy start time etc from the input
	cap->m_timescale = 1;
	cap->m_startTimestamp = clk->m_startTimestamp;
	cap->m_startFemtoseconds = clk->m_startFemtoseconds;
	cap->m_triggerPhase = 0;

	//Calculate bounds
	m_max = max(m_max, (float)vmax);
	m_min = min(m_min, (float)vmin);
	m_range = (m_max - m_min) * 1.05;
	m_offset = -( (m_max - m_min)/2 + m_min );
}
