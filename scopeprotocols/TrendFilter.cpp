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
#include "TrendFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

TrendFilter::TrendFilter(const string& color)
	: Filter(color, CAT_MATH)
	, m_tlast(0)
	, m_depthname("Buffer length")
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput("din");

	m_parameters[m_depthname] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLEDEPTH));
	m_parameters[m_depthname].SetIntVal(10000);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool TrendFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if(i > 0)
		return false;

	if(stream.GetType() == Stream::STREAM_TYPE_ANALOG_SCALAR)
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string TrendFilter::GetProtocolName()
{
	return "Trend";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void TrendFilter::ClearSweeps()
{
	SetData(nullptr, 0);
}

void TrendFilter::Refresh()
{
	m_streams[0].m_yAxisUnit = GetInput(0).GetYAxisUnits();

	//See if we have output already
	double now = GetTime();
	auto wfm = dynamic_cast<SparseAnalogWaveform*>(GetData(0));
	if(wfm)
	{
		//Remove old samples
		size_t nmax = m_parameters[m_depthname].GetIntVal();
		while(wfm->m_samples.size() > nmax)
		{
			wfm->m_samples.pop_front();
			wfm->m_durations.pop_front();
			wfm->m_offsets.pop_front();
		}
	}
	else
	{
		wfm = new SparseAnalogWaveform;
		SetData(wfm, 0);

		wfm->m_triggerPhase = 0;
		wfm->m_timescale = 1;
		m_tlast = now;
	}
	wfm->PrepareForCpuAccess();
	wfm->m_revision ++;

	//Update timestamp
	wfm->m_startTimestamp = floor(now);
	wfm->m_startFemtoseconds = (now - wfm->m_startTimestamp) * FS_PER_SECOND;

	//Update duration of previous sample
	size_t len = wfm->m_samples.size();
	double dt = (now - m_tlast) * FS_PER_SECOND;
	if(len > 0)
		wfm->m_durations[len-1] = dt;

	//Add the new sample
	wfm->m_samples.push_back(GetInput(0).GetScalarValue());
	wfm->m_durations.push_back(dt);
	if(wfm->m_offsets.empty())
		wfm->m_offsets.push_back(0);
	else
		wfm->m_offsets.push_back(dt + wfm->m_offsets[len-1]);

	//Update offsets of old samples
	len = wfm->m_samples.size();
	for(size_t i=0; i<len; i++)
		wfm->m_offsets[i] -= dt;

	wfm->MarkModifiedFromCpu();

	m_tlast = now;
}
