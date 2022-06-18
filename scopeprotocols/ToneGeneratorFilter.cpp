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
#include "ToneGeneratorFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ToneGeneratorFilter::ToneGeneratorFilter(const string& color)
	: Filter(color, CAT_GENERATION)
	, m_ratename("Sample Rate")
	, m_freqname("Frequency")
	, m_biasname("DC Bias")
	, m_amplitudename("Amplitude")
	, m_depthname("Depth")
	, m_phasename("Starting Phase")
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);

	m_parameters[m_ratename] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLERATE));
	m_parameters[m_ratename].SetIntVal(100 * 1000L * 1000L * 1000L);

	m_parameters[m_freqname] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_HZ));
	m_parameters[m_freqname].SetIntVal(100 * 1000L * 1000L);

	m_parameters[m_biasname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_biasname].SetFloatVal(0);

	m_parameters[m_amplitudename] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_amplitudename].SetFloatVal(1);

	m_parameters[m_depthname] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLEDEPTH));
	m_parameters[m_depthname].SetIntVal(100 * 1000);

	m_parameters[m_phasename] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_DEGREES));
	m_parameters[m_phasename].SetFloatVal(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool ToneGeneratorFilter::ValidateChannel(size_t /*i*/, StreamDescriptor /*stream*/)
{
	//no inputs
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string ToneGeneratorFilter::GetProtocolName()
{
	return "Sine";
}

void ToneGeneratorFilter::SetDefaultName()
{
	Unit hz(Unit::UNIT_HZ);

	char hwname[256];
	snprintf(hwname, sizeof(hwname), "Sine(%s)", hz.PrettyPrint(m_parameters[m_freqname].GetIntVal()).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void ToneGeneratorFilter::Refresh()
{
	int64_t samplerate = m_parameters[m_ratename].GetIntVal();
	int64_t freq = m_parameters[m_freqname].GetIntVal();
	size_t samplePeriod = FS_PER_SECOND / samplerate;
	float bias = m_parameters[m_biasname].GetFloatVal();
	float amplitude = m_parameters[m_amplitudename].GetFloatVal();
	size_t depth = m_parameters[m_depthname].GetIntVal();
	float startphase_deg = m_parameters[m_phasename].GetFloatVal();
	float startphase = startphase_deg * 2 * M_PI / 360;

	double t = GetTime();
	int64_t fs = (t - floor(t)) * FS_PER_SECOND;

	AnalogWaveform* cap = dynamic_cast<AnalogWaveform*>(GetData(0));
	if(!cap)
	{
		cap = new AnalogWaveform;
		SetData(cap, 0);
	}
	cap->m_timescale = samplePeriod;
	cap->m_triggerPhase = 0;
	cap->m_startTimestamp = floor(t);
	cap->m_startFemtoseconds = fs;
	cap->m_densePacked = true;
	cap->Resize(depth);

	double samples_per_cycle = samplerate * 1.0 / freq;
	double radians_per_sample = 2 * M_PI / samples_per_cycle;

	//sin is +/- 1, so need to divide amplitude by 2 to get scaling factor
	float scale = amplitude / 2;

	for(size_t i=0; i<depth; i++)
	{
		cap->m_offsets[i] = i;
		cap->m_durations[i] = 1;

		cap->m_samples[i] = bias + (scale * sin(i*radians_per_sample + startphase));
	}
}
