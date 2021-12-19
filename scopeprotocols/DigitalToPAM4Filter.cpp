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
#include "DigitalToPAM4Filter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DigitalToPAM4Filter::DigitalToPAM4Filter(const string& color)
	: WaveformGenerationFilter(color)
	, m_level00("Level 00")
	, m_level01("Level 01")
	, m_level10("Level 10")
	, m_level11("Level 11")
{
	m_parameters[m_level00] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_level00].SetFloatVal(-0.3);

	m_parameters[m_level01] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_level01].SetFloatVal(-0.1);

	m_parameters[m_level10] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_level10].SetFloatVal(0.1);

	m_parameters[m_level11] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_level11].SetFloatVal(0.3);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string DigitalToPAM4Filter::GetProtocolName()
{
	return "Digital to PAM4";
}

void DigitalToPAM4Filter::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "DigitalToPAM4(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

float DigitalToPAM4Filter::GetVoltageRange(size_t /*stream*/)
{
	float v0 = m_parameters[m_level00].GetFloatVal();
	float v1 = m_parameters[m_level01].GetFloatVal();
	float v2 = m_parameters[m_level10].GetFloatVal();
	float v3 = m_parameters[m_level11].GetFloatVal();
	float vmin = min(v0, v1);
	vmin = min(vmin, v2);
	vmin = min(vmin, v3);
	float vmax = max(v0, v1);
	vmax = max(vmax, v2);
	vmax = max(vmax, v3);

	return fabs(vmax - vmin) * 1.05;
}

float DigitalToPAM4Filter::GetOffset(size_t stream)
{
	float v0 = m_parameters[m_level00].GetFloatVal();
	float v1 = m_parameters[m_level01].GetFloatVal();
	float v2 = m_parameters[m_level10].GetFloatVal();
	float v3 = m_parameters[m_level11].GetFloatVal();
	float vmin = min(v0, v1);
	vmin = min(vmin, v2);
	vmin = min(vmin, v3);
	float vmax = max(v0, v1);
	vmax = max(vmax, v2);
	vmax = max(vmax, v3);

	return -GetVoltageRange(stream) + (vmax-vmin)/2;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

size_t DigitalToPAM4Filter::GetBitsPerSymbol()
{
	return 2;
}

vector<float> DigitalToPAM4Filter::GetVoltageLevels()
{
	vector<float> ret;
	ret.push_back(m_parameters[m_level00].GetFloatVal());
	ret.push_back(m_parameters[m_level01].GetFloatVal());
	ret.push_back(m_parameters[m_level10].GetFloatVal());
	ret.push_back(m_parameters[m_level11].GetFloatVal());
	return ret;
}

size_t DigitalToPAM4Filter::GetVoltageCode(size_t i, DigitalWaveform& samples)
{
	size_t code = 0;
	if(samples.m_samples[i])
		code |= 2;
	if(samples.m_samples[i+1])
		code |= 1;
	return code;
}
