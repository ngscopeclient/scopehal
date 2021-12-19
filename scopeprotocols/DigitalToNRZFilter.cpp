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
#include "DigitalToNRZFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DigitalToNRZFilter::DigitalToNRZFilter(const string& color)
	: WaveformGenerationFilter(color)
	, m_level0("Level 0")
	, m_level1("Level 1")
{
	m_parameters[m_level0] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_level0].SetFloatVal(0);

	m_parameters[m_level1] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_level1].SetFloatVal(1.8);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string DigitalToNRZFilter::GetProtocolName()
{
	return "Digital to NRZ";
}

void DigitalToNRZFilter::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "DigitalToNRZ(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

float DigitalToNRZFilter::GetVoltageRange(size_t /*stream*/)
{
	float v0 = m_parameters[m_level0].GetFloatVal();
	float v1 = m_parameters[m_level1].GetFloatVal();

	return fabs(v1 - v0) * 1.05;
}

float DigitalToNRZFilter::GetOffset(size_t stream)
{
	float v0 = m_parameters[m_level0].GetFloatVal();
	float v1 = m_parameters[m_level1].GetFloatVal();

	return -GetVoltageRange(stream) + (v0+v1)/2;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

size_t DigitalToNRZFilter::GetBitsPerSymbol()
{
	return 1;
}

vector<float> DigitalToNRZFilter::GetVoltageLevels()
{
	vector<float> ret;
	ret.push_back(m_parameters[m_level0].GetFloatVal());
	ret.push_back(m_parameters[m_level1].GetFloatVal());
	return ret;
}

size_t DigitalToNRZFilter::GetVoltageCode(size_t i, DigitalWaveform& samples)
{
	return samples.m_samples[i];
}
