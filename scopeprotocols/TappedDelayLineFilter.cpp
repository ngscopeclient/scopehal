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
#include "TappedDelayLineFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

TappedDelayLineFilter::TappedDelayLineFilter(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MATH)
	, m_tapDelayName("Tap Delay")
	, m_tap0Name("Tap Value 0")
	, m_tap1Name("Tap Value 1")
	, m_tap2Name("Tap Value 2")
	, m_tap3Name("Tap Value 3")
	, m_tap4Name("Tap Value 4")
	, m_tap5Name("Tap Value 5")
	, m_tap6Name("Tap Value 6")
	, m_tap7Name("Tap Value 7")
{
	CreateInput("in");

	m_range = 1;
	m_offset = 0;
	m_min = FLT_MAX;
	m_max = -FLT_MAX;

	m_parameters[m_tapDelayName] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));
	m_parameters[m_tapDelayName].SetIntVal(200000);

	m_parameters[m_tap0Name] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_tap0Name].SetFloatVal(1);

	m_parameters[m_tap1Name] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_tap1Name].SetFloatVal(0);

	m_parameters[m_tap2Name] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_tap2Name].SetFloatVal(0);

	m_parameters[m_tap3Name] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_tap3Name].SetFloatVal(0);

	m_parameters[m_tap4Name] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_tap4Name].SetFloatVal(0);

	m_parameters[m_tap5Name] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_tap5Name].SetFloatVal(0);

	m_parameters[m_tap6Name] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_tap6Name].SetFloatVal(0);

	m_parameters[m_tap7Name] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_tap7Name].SetFloatVal(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool TappedDelayLineFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void TappedDelayLineFilter::ClearSweeps()
{
	m_range = 1;
	m_offset = 0;
	m_min = FLT_MAX;
	m_max = -FLT_MAX;
}

void TappedDelayLineFilter::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "TappedDelayLine(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string TappedDelayLineFilter::GetProtocolName()
{
	return "Tapped Delay Line";
}

bool TappedDelayLineFilter::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool TappedDelayLineFilter::NeedsConfig()
{
	return true;
}

double TappedDelayLineFilter::GetVoltageRange()
{
	return m_range;
}

double TappedDelayLineFilter::GetOffset()
{
	return m_offset;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void TappedDelayLineFilter::Refresh()
{
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din = GetAnalogInputWaveform(0);
	size_t len = din->m_samples.size();
	if(len < 8)
	{
		SetData(NULL, 0);
		return;
	}
	m_xAxisUnit = m_inputs[0].m_channel->GetXAxisUnits();
	m_yAxisUnit = m_inputs[0].m_channel->GetYAxisUnits();

	//Set up output
	auto cap = new AnalogWaveform;
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	SetData(cap, 0);

	//Get the tap config
	int64_t tap_delay = m_parameters[m_tapDelayName].GetIntVal();

	//Extract tap values
	float taps[8] =
	{
		m_parameters[m_tap0Name].GetFloatVal(),
		m_parameters[m_tap1Name].GetFloatVal(),
		m_parameters[m_tap2Name].GetFloatVal(),
		m_parameters[m_tap3Name].GetFloatVal(),
		m_parameters[m_tap4Name].GetFloatVal(),
		m_parameters[m_tap5Name].GetFloatVal(),
		m_parameters[m_tap6Name].GetFloatVal(),
		m_parameters[m_tap7Name].GetFloatVal()
	};

	//Run the actual filter
	float vmin;
	float vmax;
	DoFilterKernel(tap_delay, taps, din, cap, vmin, vmax);

	//Calculate bounds
	m_max = max(m_max, vmax);
	m_min = min(m_min, vmin);
	m_range = (m_max - m_min) * 1.05;
	m_offset = -( (m_max - m_min)/2 + m_min );
}

void TappedDelayLineFilter::DoFilterKernel(
	int64_t tap_delay,
	float* taps,
	AnalogWaveform* din,
	AnalogWaveform* cap,
	float& vmin,
	float& vmax)
{
	if(g_hasAvx2)
		DoFilterKernelAVX2(tap_delay, taps, din, cap, vmin, vmax);
	else
		DoFilterKernelGeneric(tap_delay, taps, din, cap, vmin, vmax);
}

void TappedDelayLineFilter::DoFilterKernelGeneric(
	int64_t tap_delay,
	float* taps,
	AnalogWaveform* din,
	AnalogWaveform* cap,
	float& vmin,
	float& vmax)
{
	//For now, no resampling. Assume tap delay is an integer number of samples.
	int64_t samples_per_tap = tap_delay / cap->m_timescale;

	//Setup
	vmin = FLT_MAX;
	vmax = -FLT_MAX;
	size_t len = din->m_samples.size();
	size_t filterlen = 8*samples_per_tap;
	size_t end = len - filterlen;
	cap->Resize(end);

	//Copy the timestamps
	memcpy(&cap->m_offsets[0], &din->m_offsets[filterlen], end*sizeof(int64_t));
	memcpy(&cap->m_durations[0], &din->m_durations[filterlen], end*sizeof(int64_t));

	//Do the filter
	for(size_t i=0; i<end; i++)
	{
		float v = 0;
		for(int64_t j=0; j<8; j++)
			v += din->m_samples[i + j*samples_per_tap] * taps[7 - j];

		vmin = min(vmin, v);
		vmax = max(vmax, v);

		cap->m_samples[i]	= v;
	}
}

void TappedDelayLineFilter::DoFilterKernelAVX2(
	int64_t tap_delay,
	float* taps,
	AnalogWaveform* din,
	AnalogWaveform* cap,
	float& vmin,
	float& vmax)
{
	//For now, no resampling. Assume tap delay is an integer number of samples.
	int64_t samples_per_tap = tap_delay / cap->m_timescale;

	//Setup
	vmin = FLT_MAX;
	vmax = -FLT_MAX;
	size_t len = din->m_samples.size();
	size_t filterlen = 8*samples_per_tap;
	size_t end = len - filterlen;
	cap->Resize(end);

	//Copy the timestamps
	memcpy(&cap->m_offsets[0], &din->m_offsets[filterlen], end*sizeof(int64_t));
	memcpy(&cap->m_durations[0], &din->m_durations[filterlen], end*sizeof(int64_t));

	//Do the filter
	for(size_t i=0; i<end; i++)
	{
		float v = 0;
		for(int64_t j=0; j<8; j++)
			v += din->m_samples[i + j*samples_per_tap] * taps[7 - j];

		vmin = min(vmin, v);
		vmax = max(vmax, v);

		cap->m_samples[i]	= v;
	}
}
