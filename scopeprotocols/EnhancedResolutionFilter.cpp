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
#include "EnhancedResolutionFilter.h"
#include <ffts.h>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

EnhancedResolutionFilter::EnhancedResolutionFilter(const string& color)
	: FIRFilter(color)
	, m_cutoffFreqName("Cutoff Frequency")
	, m_bitsName("Bits")
{
	m_parameters[m_filterTypeName].MarkHidden();
	m_parameters[m_filterLengthName].MarkHidden();
	m_parameters[m_stopbandAttenName].MarkHidden();
	m_parameters[m_freqLowName].MarkHidden();
	m_parameters[m_freqHighName].MarkHidden();

	m_parameters[m_bitsName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_bitsName].AddEnumValue("0.5", BITS_0P5);
	m_parameters[m_bitsName].AddEnumValue("1.0", BITS_1P0);
	m_parameters[m_bitsName].AddEnumValue("1.5", BITS_1P5);
	m_parameters[m_bitsName].AddEnumValue("2.0", BITS_2P0);
	m_parameters[m_bitsName].AddEnumValue("2.5", BITS_2P5);
	m_parameters[m_bitsName].AddEnumValue("3.0", BITS_3P0);
	m_parameters[m_bitsName].SetIntVal(BITS_0P5);
	m_parameters[m_bitsName].signal_changed().connect(sigc::mem_fun(*this, &EnhancedResolutionFilter::OnBitsChanged));

	m_parameters[m_cutoffFreqName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ));
	m_parameters[m_cutoffFreqName].SetFloatVal(0);

	m_parameters[m_cutoffFreqName].MarkReadOnly();

	m_parameters[m_filterTypeName].SetIntVal(FILTER_TYPE_LOWPASS);

	OnBitsChanged();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string EnhancedResolutionFilter::GetProtocolName()
{
	return "Enhanced Resolution";
}

void EnhancedResolutionFilter::SetDefaultName()
{
	string name = string("Eres(") + GetInputDisplayName(0) + ", ";

	switch(m_parameters[m_bitsName].GetIntVal())
	{
		case BITS_0P5:
			name += "0.5";
			break;

		case BITS_1P0:
			name += "1.0";
			break;

		case BITS_1P5:
			name += "1.5";
			break;

		case BITS_2P0:
			name += "2.0";
			break;

		case BITS_2P5:
			name += "2.5";
			break;

		case BITS_3P0:
			name += "3.0";
			break;
	}

	name += ")";
	m_hwname = name;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void EnhancedResolutionFilter::Refresh(vk::raii::CommandBuffer& cmdBuf, vk::raii::Queue& queue)
{
	UpdateCutoff();
	FIRFilter::Refresh(cmdBuf, queue);
}

void EnhancedResolutionFilter::OnBitsChanged()
{
	UpdateCutoff();
}

void EnhancedResolutionFilter::UpdateCutoff()
{
	if(!VerifyAllInputsOKAndUniformAnalog())
		return;

	auto din = GetInputWaveform(0);
	int64_t fs_per_sample = din->m_timescale;
	float sample_hz = FS_PER_SECOND / fs_per_sample;

	float nyquist = sample_hz / 2;

	//Cutoff frequency depends on bit resolution
	//Each extra half bit of resolution divides the cutoff frequency by 2
	float freq = 0;
	switch(m_parameters[m_bitsName].GetIntVal())
	{
		case BITS_0P5:
			freq = nyquist / 2;
			break;

		case BITS_1P0:
			freq = nyquist / 4;
			break;

		case BITS_1P5:
			freq = nyquist / 8;
			break;

		case BITS_2P0:
			freq = nyquist / 16;
			break;

		case BITS_2P5:
			freq = nyquist / 32;
			break;

		case BITS_3P0:
			freq = nyquist / 64;
			break;
	}

	m_parameters[m_cutoffFreqName].SetFloatVal(freq);
	m_parameters[m_freqHighName].SetFloatVal(freq);
}
