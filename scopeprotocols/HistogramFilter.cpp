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
#include "HistogramFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

HistogramFilter::HistogramFilter(const string& color)
	: Filter(color, CAT_MATH),
	  m_autorangeName("Autorange?"),
	  m_minName("Min Value"),
	  m_maxName("Max Value"),
	  m_binSizeName("Bin Size")
{
	AddStream(Unit(Unit::UNIT_COUNTS_SCI), "data", Stream::STREAM_TYPE_ANALOG);

	m_parameters[m_autorangeName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_autorangeName].AddEnumValue("Autorange", 1);
	m_parameters[m_autorangeName].AddEnumValue("Manual Range", 0);
	m_parameters[m_autorangeName].SetIntVal(1);

	m_parameters[m_minName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_FS));
	m_parameters[m_minName].SetIntVal(0);

	m_parameters[m_maxName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_FS));
	m_parameters[m_maxName].SetIntVal(100);

	m_parameters[m_binSizeName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_FS));
	m_parameters[m_binSizeName].SetIntVal(100);
	// Retain existing default behavior of 100fs bins

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
	if(stream.GetType() == Stream::STREAM_TYPE_ANALOG)
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

float HistogramFilter::GetVoltageRange(size_t /*stream*/)
{
	return m_range;
}

float HistogramFilter::GetOffset(size_t /*stream*/)
{
	return -m_midpoint;
}

void HistogramFilter::SetVoltageRange(float range, size_t /*stream*/)
{
	m_range = range;
}

void HistogramFilter::SetOffset(float offset, size_t /*stream*/)
{
	m_midpoint = -offset;
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
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	auto din = GetInputWaveform(0);
	din->PrepareForCpuAccess();
	auto sdin = dynamic_cast<SparseAnalogWaveform*>(din);
	auto udin = dynamic_cast<UniformAnalogWaveform*>(din);

	m_xAxisUnit = GetInput(0).GetYAxisUnits();

	//Calculate min/max of the input data
	float nmin = GetMinVoltage(sdin, udin);
	float nmax = GetMaxVoltage(sdin, udin);

	//Calculate bin count
	auto cap = dynamic_cast<UniformAnalogWaveform*>(GetData(0));

	bool reallocate = false;
	float range = m_max - m_min;

	if (m_parameters[m_autorangeName].GetIntVal())
	{
		//If the signal is outside our current range, extend our range
		if( (nmin < m_min) || (nmax > m_max) )
		{
			m_min = min(nmin, m_min);
			m_max = max(nmax, m_max);

			//Extend the range by a bit to avoid constant reallocation
			range = m_max - m_min;
			m_min -= 0.05 * range;
			m_max += 0.05 * range;

			// m_parameters[m_minName].SetFloatVal(m_min);
			// m_parameters[m_maxName].SetFloatVal(m_max);
			// TODO: This would be nice UX but locks up the UI on .emit()

			reallocate = true;
		}
	}
	else
	{
		float newMin = m_parameters[m_minName].GetFloatVal();
		float newMax = m_parameters[m_maxName].GetFloatVal();

		m_min = newMin;
		m_max = newMax;

		reallocate = (newMin != m_min) || (newMax != m_max);
	}

	reallocate |= (cap == nullptr);
	// Always need to reallocate if don't have an output yet

	range = m_max - m_min;

	bool didClipRange = (nmin < m_min) || (nmax > m_max);

	size_t bins = ceil(range) / m_parameters[m_binSizeName].GetFloatVal();

	// arbitrary sanity-check bounds
	if (bins < 1) bins = 1;
	if (bins > 10000) bins = 10000;

	//Calculate histogram for our incoming data
	auto data = MakeHistogram(sdin, udin, m_min, m_max, bins);

	//Calculate bin configuration.
	float binsize = range / bins;

	//Reallocate the histogram if we changed it
	if(reallocate)
	{
		//Reallocate our waveform
		cap = new UniformAnalogWaveform;
		cap->m_timescale = binsize;
		cap->m_startTimestamp = din->m_startTimestamp;
		cap->m_startFemtoseconds = din->m_startFemtoseconds;
		cap->m_triggerPhase = m_min;
		cap->m_flags = 0; // Updated at end
		SetData(cap, 0);

		cap->Resize(bins);
		cap->PrepareForCpuAccess();

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

	cap->m_flags |= didClipRange ? WaveformBase::WAVEFORM_CLIPPING : 0;

	cap->MarkModifiedFromCpu();
}
