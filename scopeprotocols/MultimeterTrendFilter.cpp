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

#include "../scopehal/scopehal.h"
#include "MultimeterTrendFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

MultimeterTrendFilter::MultimeterTrendFilter(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MEASUREMENT)
	, m_meter(NULL)
{
	ClearStreams();
	AddStream(Unit(Unit::UNIT_VOLTS), "Primary");
	AddStream(Unit(Unit::UNIT_VOLTS), "Secondary");

	//initial default config until we have data
	SetVoltageRange(1, 0);
	SetOffset(0, 0);
	SetVoltageRange(1, 1);
	SetOffset(0, 1);

	SetXAxisUnits(Unit(Unit::UNIT_FS));
	m_tlast = GetTime();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool MultimeterTrendFilter::ValidateChannel(size_t /*i*/, StreamDescriptor /*stream*/)
{
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string MultimeterTrendFilter::GetProtocolName()
{
	return "Multimeter Trend";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void MultimeterTrendFilter::ClearSweeps()
{
	SetData(NULL, 0);
	SetData(NULL, 1);
}

AnalogWaveform* MultimeterTrendFilter::GetWaveform(size_t stream)
{
	auto wfm = dynamic_cast<AnalogWaveform*>(GetData(stream));
	if(wfm == NULL)
	{
		wfm = new AnalogWaveform;
		SetData(wfm, stream);

		//Base time unit is milliseconds, and sampling is irregular
		wfm->m_timescale = FS_PER_SECOND / 1000;
		wfm->m_densePacked = false;
		wfm->m_triggerPhase = false;
		wfm->m_flags = 0;
	}

	return wfm;
}

void MultimeterTrendFilter::Refresh()
{
	//nothing to do
}

void MultimeterTrendFilter::OnDataReady(double prival, double secval)
{
	//Get output waveforms, creating if needed
	double now = GetTime();
	auto pri = GetWaveform(0);
	auto sec = GetWaveform(1);

	//Update units and clear if needed
	auto punit = m_meter->GetMeterUnit();
	if(punit != GetYAxisUnits(0))
	{
		pri->clear();
		SetYAxisUnits(punit, 0);
	}

	auto sunit = m_meter->GetSecondaryMeterUnit();
	if(sunit != GetYAxisUnits(1))
	{
		sec->clear();
		SetYAxisUnits(sunit, 1);
	}

	AddSample(pri, prival, now);
	AddSample(sec, secval, now);

	m_tlast = now;
}

void MultimeterTrendFilter::AddSample(AnalogWaveform* wfm, double value, double now)
{
	//Remove old samples
	size_t nmax = 4096;
	while(wfm->m_samples.size() > nmax)
	{
		wfm->m_samples.erase(wfm->m_samples.begin());
		wfm->m_durations.erase(wfm->m_durations.begin());
		wfm->m_offsets.erase(wfm->m_offsets.begin());
	}

	//Update timestamp
	wfm->m_startTimestamp = floor(now);
	wfm->m_startFemtoseconds = (now - wfm->m_startTimestamp) * FS_PER_SECOND;

	//Update duration of previous sample
	size_t len = wfm->m_samples.size();
	double dt = (now - m_tlast) * (FS_PER_SECOND / wfm->m_timescale);
	if(len > 0)
		wfm->m_durations[len-1] = dt;

	//Add the new sample
	wfm->m_samples.push_back(value);
	wfm->m_durations.push_back(dt);
	if(wfm->m_offsets.empty())
		wfm->m_offsets.push_back(0);
	else
		wfm->m_offsets.push_back(dt + wfm->m_offsets[len-1]);

	//Update offsets of old samples
	len = wfm->m_samples.size();
	for(size_t i=0; i<len; i++)
		wfm->m_offsets[i] -= dt;
}
