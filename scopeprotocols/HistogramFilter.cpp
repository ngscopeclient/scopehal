/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2021 Andrew D. Zonenberg and contributors                                                         *
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
#include "HistogramFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

HistogramFilter::HistogramFilter(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MATH)
{
	m_yAxisUnit = Unit(Unit::UNIT_COUNTS_SCI);

	//Set up channels
	CreateInput("data");

	m_midpoint = 0.5;
	m_range = 1;

	ClearSweeps();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool HistogramFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if(i > 0)
		return false;
	if(stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG)
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void HistogramFilter::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "Histogram(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string HistogramFilter::GetProtocolName()
{
	return "Histogram";
}

bool HistogramFilter::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool HistogramFilter::NeedsConfig()
{
	//automatic configuration
	return false;
}

double HistogramFilter::GetVoltageRange()
{
	return m_range;
}

double HistogramFilter::GetOffset()
{
	return -m_midpoint;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void HistogramFilter::ClearSweeps()
{
	m_min = FLT_MAX;
	m_max = -FLT_MAX;
	m_histogram.clear();
	SetData(NULL, 0);
}

void HistogramFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	auto din = GetAnalogInputWaveform(0);
	m_xAxisUnit = GetInput(0).m_channel->GetYAxisUnits();

	//Calculate min/max of the input data
	float nmin = FLT_MAX;
	float nmax = -FLT_MAX;
	for(float v : din->m_samples)
	{
		nmin = min(nmin, v);
		nmax = max(nmax, v);
	}

	//Calculate bin count
	auto cap = dynamic_cast<AnalogWaveform*>(GetData(0));

	//If the signal is outside our current range, extend our range
	bool reallocate = false;
	float range = m_max - m_min;
	if( (nmin < m_min) || (nmax > m_max) || (cap == NULL) )
	{
		m_min = min(nmin, m_min);
		m_max = max(nmax, m_max);

		//Extend the range by a bit to avoid constant reallocation
		range = m_max - m_min;
		m_min -= 0.05 * range;
		m_max += 0.05 * range;
		range = m_max - m_min;

		reallocate = true;
	}

	//Calculate histogram for our incoming data
	//For now, 100fs per bin target
	size_t bins = ceil(range) / 100;
	auto data = MakeHistogram(din, m_min, m_max, bins);

	//Calculate bin configuration.
	//Clip bin size to nearest ps (this will stop being a problem when we move to fs)
	float binsize = range / bins;

	//Reallocate the histogram if we changed it
	if(reallocate)
	{
		//Reallocate our waveform
		cap = new AnalogWaveform;
		cap->m_timescale = 1;
		cap->m_startTimestamp = din->m_startTimestamp;
		cap->m_startFemtoseconds = din->m_startFemtoseconds;
		SetData(cap, 0);

		//Set up timestamps and initial values
		for(size_t i=0; i<bins; i++)
		{
			cap->m_offsets.push_back(m_min + binsize*i);
			cap->m_durations.push_back(binsize);
			cap->m_samples.push_back(0);
		}

		m_histogram.clear();
		for(size_t i=0; i<bins; i++)
			m_histogram.push_back(0);
	}

	//Update histogram
	size_t vmax = 0;
	for(size_t i=0; i<bins; i++)
	{
		m_histogram[i] += data[i];
		vmax = max(vmax, m_histogram[i]);
	}

	//Generate output
	for(size_t i=0; i<bins; i++)
		cap->m_samples[i] 	= m_histogram[i];

	vmax *= 1.05;
	m_range = vmax + 2;
	m_midpoint = m_range/2;
}
