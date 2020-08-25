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
#include <complex>
#include "OFDMDemodulator.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

OFDMDemodulator::OFDMDemodulator(string color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_RF)
{
	//Set up channels
	CreateInput("I");
	CreateInput("Q");

	m_range = 1;
	m_offset = 0;

	m_min = FLT_MAX;
	m_max = -FLT_MAX;

	/*
	m_windowName = "Window";
	m_parameters[m_windowName] = FilterParameter(FilterParameter::TYPE_FLOAT);
	m_parameters[m_windowName].SetFloatVal(0.4e-6);

	m_periodName = "Period";
	m_parameters[m_periodName] = FilterParameter(FilterParameter::TYPE_FLOAT);
	m_parameters[m_periodName].SetFloatVal(3.6e-6);
	*/
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool OFDMDemodulator::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 2) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

double OFDMDemodulator::GetVoltageRange()
{
	return m_range;
}

double OFDMDemodulator::GetOffset()
{
	return -m_offset;
}

string OFDMDemodulator::GetProtocolName()
{
	return "OFDM Demodulator";
}

bool OFDMDemodulator::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool OFDMDemodulator::NeedsConfig()
{
	return true;
}

void OFDMDemodulator::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "OFDM(%s, %s)",
		GetInputDisplayName(0).c_str(),
		GetInputDisplayName(1).c_str()
		);

	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void OFDMDemodulator::ClearSweeps()
{
	m_range = 1;
	m_offset = 0;
	m_min = FLT_MAX;
	m_max = -FLT_MAX;
}

void OFDMDemodulator::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the sample data
	auto din_i = GetAnalogInputWaveform(0);
	auto din_q = GetAnalogInputWaveform(1);

	//TODO
	SetData(NULL, 0);
	return;

	/*
	//Copy the units
	m_yAxisUnit = m_channels[0]->GetYAxisUnits();

	//Convert window and period to samples
	float window_ps = m_parameters[m_windowName].GetFloatVal() * 1e12;
	size_t window_samples = window_ps / din_i->m_timescale;
	float period_ps = m_parameters[m_periodName].GetFloatVal() * 1e12;
	size_t period_samples = period_ps / din_i->m_timescale;
	window_samples = min(window_samples, period_samples);

	//We need meaningful data, bail if it's too short
	auto len = min(din_i->m_samples.size(), din_q->m_samples.size());
	if(len < period_samples)
	{
		SetData(NULL);
		return;
	}

	//Set up the output waveform
	auto cap = new AnalogWaveform;

	size_t end = len - 2*period_samples;
	float vmax = -FLT_MAX;
	float vmin = FLT_MAX;
	for(size_t i=0; i < end; i ++)
	{
		complex<float> total = 0;
		for(size_t j=0; j<window_samples; j++)
		{
			size_t first = i + j;
			size_t second = first + period_samples;

			complex<float> a(din_i->m_samples[first], din_q->m_samples[first]);
			complex<float> b(din_i->m_samples[second], din_q->m_samples[second]);

			total += a*b;
		}

		float v = abs(total) / window_samples;
		vmax = max(vmax, v);
		vmin = min(vmin, v);

		cap->m_samples.push_back(v);
		cap->m_offsets.push_back(din_i->m_offsets[i]);
		cap->m_durations.push_back(din_i->m_durations[i]);
	}

	//Calculate bounds
	m_max = max(m_max, vmax);
	m_min = min(m_min, vmin);
	m_range = (m_max - m_min) * 1.05;
	m_offset = ( (m_max - m_min)/2 + m_min );

	//Copy our time scales from the input
	cap->m_timescale 		= din_i->m_timescale;
	cap->m_startTimestamp 	= din_i->m_startTimestamp;
	cap->m_startPicoseconds = din_i->m_startPicoseconds;

	SetData(cap);
	*/
}
