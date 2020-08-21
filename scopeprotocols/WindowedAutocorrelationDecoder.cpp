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
#include "WindowedAutocorrelationDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

WindowedAutocorrelationDecoder::WindowedAutocorrelationDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MATH)
{
	//Set up channels
	m_signalNames.push_back("din");
	m_channels.push_back(NULL);

	m_range = 1;
	m_offset = 0;

	m_min = FLT_MAX;
	m_max = -FLT_MAX;

	m_windowName = "Window";
	m_parameters[m_windowName] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_FLOAT);
	m_parameters[m_windowName].SetFloatVal(3.2e-6);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool WindowedAutocorrelationDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i < 1) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

double WindowedAutocorrelationDecoder::GetVoltageRange()
{
	return m_range;
}

double WindowedAutocorrelationDecoder::GetOffset()
{
	return -m_offset;
}

string WindowedAutocorrelationDecoder::GetProtocolName()
{
	return "Windowed Autocorrelation";
}

bool WindowedAutocorrelationDecoder::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool WindowedAutocorrelationDecoder::NeedsConfig()
{
	return true;
}

void WindowedAutocorrelationDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "WindowedAutocorrelation(%s)",
		m_channels[0]->m_displayname.c_str());

	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void WindowedAutocorrelationDecoder::ClearSweeps()
{
	m_range = 1;
	m_offset = 0;
	m_min = FLT_MAX;
	m_max = -FLT_MAX;
}

void WindowedAutocorrelationDecoder::Refresh()
{
	//Get the input data
	if(m_channels[0] == NULL)
	{
		SetData(NULL);
		return;
	}

	auto din = dynamic_cast<AnalogWaveform*>(m_channels[0]->GetData());
	if(!din)
	{
		SetData(NULL);
		return;
	}

	//Copy the units
	m_yAxisUnit = m_channels[0]->GetYAxisUnits();

	//Convert range to samples
	float range_ps = m_parameters[m_windowName].GetFloatVal() * 1e12;
	size_t range_samples = range_ps / din->m_timescale;

	//We need meaningful data
	auto len = din->m_samples.size();
	if(len < range_samples)
	{
		SetData(NULL);
		return;
	}

	//Set up the output waveform
	auto cap = new AnalogWaveform;

	size_t end = len - 2*range_samples;
	float vmax = -FLT_MAX;
	float vmin = FLT_MAX;
	for(size_t i=0; i < end; i ++)
	{
		double total = 0;
		for(size_t j=0; j<range_samples; j++)
			total += din->m_samples[i+j] * din->m_samples[i+range_samples+j];

		float v = total / range_samples;
		vmax = max(vmax, v);
		vmin = min(vmin, v);

		cap->m_samples.push_back(v);
		cap->m_offsets.push_back(i);
		cap->m_durations.push_back(1);
	}

	//Calculate bounds
	m_max = max(m_max, vmax);
	m_min = min(m_min, vmin);
	m_range = (m_max - m_min) * 1.05;
	m_offset = ( (m_max - m_min)/2 + m_min );

	//Copy our time scales from the input
	cap->m_timescale 		= din->m_timescale;
	cap->m_startTimestamp 	= din->m_startTimestamp;
	cap->m_startPicoseconds = din->m_startPicoseconds;

	SetData(cap);
}
