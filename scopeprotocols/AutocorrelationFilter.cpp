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

#include "../scopehal/scopehal.h"
#include "AutocorrelationFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

AutocorrelationFilter::AutocorrelationFilter(string color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MATH)
{
	//Set up inputs
	CreateInput("din");

	m_range = 1;
	m_offset = 0;

	m_maxDeltaName = "Max offset";
	m_parameters[m_maxDeltaName] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_maxDeltaName].SetIntVal(1000);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool AutocorrelationFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

double AutocorrelationFilter::GetVoltageRange()
{
	return m_range;
}

double AutocorrelationFilter::GetOffset()
{
	return -m_offset;
}

string AutocorrelationFilter::GetProtocolName()
{
	return "Autocorrelation";
}

bool AutocorrelationFilter::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool AutocorrelationFilter::NeedsConfig()
{
	return true;
}

void AutocorrelationFilter::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "Autocorrelation(%s)", GetInputDisplayName(0).c_str());

	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void AutocorrelationFilter::Refresh()
{
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	auto din = dynamic_cast<AnalogWaveform*>(GetInputWaveform(0));
	auto len = din->m_samples.size();

	//Copy the units
	m_yAxisUnit = m_inputs[0].m_channel->GetYAxisUnits();

	//Sanity check range
	size_t range = m_parameters[m_maxDeltaName].GetIntVal();
	if( len <= range)
	{
		SetData(NULL, 0);
		return;
	}

	//Set up the output waveform
	auto cap = new AnalogWaveform;

	size_t end = len - range;
	for(size_t delta=1; delta <= range; delta ++)
	{
		double total = 0;
		for(size_t i=0; i<end; i++)
			total += din->m_samples[i] * din->m_samples[i+delta];

		cap->m_samples.push_back(total / end);
		cap->m_offsets.push_back(delta);
		cap->m_durations.push_back(1);
	}

	//Calculate range of the output waveform
	float x = GetMaxVoltage(cap);
	float n = GetMinVoltage(cap);
	m_range = x - n;
	m_offset = (x+n)/2;

	//Copy our time scales from the input
	cap->m_timescale 		= din->m_timescale;
	cap->m_startTimestamp 	= din->m_startTimestamp;
	cap->m_startPicoseconds = din->m_startPicoseconds;

	SetData(cap, 0);
}
