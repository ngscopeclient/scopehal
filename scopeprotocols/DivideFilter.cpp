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

#include "../scopehal/scopehal.h"
#include "DivideFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DivideFilter::DivideFilter(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MATH)
	, m_formatName("Output Format")
{
	//Set up channels
	CreateInput("a");
	CreateInput("b");

	m_range = 1;
	m_offset = 0;
	m_min = FLT_MAX;
	m_max = -FLT_MAX;

	m_parameters[m_formatName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_formatName].AddEnumValue("Ratio", FORMAT_RATIO);
	m_parameters[m_formatName].AddEnumValue("dB", FORMAT_DB);
	m_parameters[m_formatName].SetIntVal(FORMAT_RATIO);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DivideFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 2) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

double DivideFilter::GetVoltageRange()
{
	return m_range;
}

double DivideFilter::GetOffset()
{
	return m_offset;
}

string DivideFilter::GetProtocolName()
{
	return "Divide";
}

bool DivideFilter::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool DivideFilter::NeedsConfig()
{
	return true;
}

void DivideFilter::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "(%s / %s)",
		GetInputDisplayName(0).c_str(),
		GetInputDisplayName(1).c_str());

	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DivideFilter::ClearSweeps()
{
	m_range = 1;
	m_offset = 0;
	m_min = FLT_MAX;
	m_max = -FLT_MAX;
}

void DivideFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto a = GetAnalogInputWaveform(0);
	auto b = GetAnalogInputWaveform(1);
	auto len = min(a->m_samples.size(), b->m_samples.size());

	//Set up the output waveform
	auto cap = SetupOutputWaveform(a, 0, 0, 0);

	float* fa = (float*)__builtin_assume_aligned(&a->m_samples[0], 16);
	float* fb = (float*)__builtin_assume_aligned(&b->m_samples[0], 16);
	float* fdst = (float*)__builtin_assume_aligned(&cap->m_samples[0], 16);

	auto format = m_parameters[m_formatName].GetIntVal();

	if(format == FORMAT_RATIO)
	{
		m_yAxisUnit = Unit(Unit::UNIT_COUNTS);

		//Divide the units
		//m_yAxisUnit = m_inputs[0].m_channel->GetYAxisUnits() / m_inputs[1].m_channel->GetYAxisUnits();

		for(size_t i=0; i<len; i++)
			fdst[i] = fa[i] / fb[i];
	}
	else /*if(format == FORMAT_DB) */
	{
		m_yAxisUnit = Unit(Unit::UNIT_DB);

		for(size_t i=0; i<len; i++)
			fdst[i] = 20 * log10(fa[i] / fb[i]);
	}

	//Calculate range of the output waveform
	float vmax = GetMaxVoltage(cap);
	float vmin = GetMinVoltage(cap);

	//Calculate bounds
	m_max = max(m_max, vmax);
	m_min = min(m_min, vmin);
	m_range = (m_max - m_min) * 1.05;
	m_offset = -( (m_max - m_min)/2 + m_min );
}
