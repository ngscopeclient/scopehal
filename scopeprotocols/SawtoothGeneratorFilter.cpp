/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2024 Andrew D. Zonenberg and contributors                                                         *
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
#include "SawtoothGeneratorFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SawtoothGeneratorFilter::SawtoothGeneratorFilter(const string& color)
	: Filter(color, CAT_GENERATION)
	, m_ratename("Sample Rate")
	, m_freqname("Frequency")
	, m_biasname("DC Bias")
	, m_amplitudename("Amplitude")
	, m_depthname("Depth")
	, m_phasename("Starting Phase")
	, m_unitname("Unit")
	, m_rampname("Direction")
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);

	m_parameters[m_ratename] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLERATE));
	m_parameters[m_ratename].SetIntVal(100 * INT64_C(1000) * INT64_C(1000) * INT64_C(1000));

	m_parameters[m_freqname] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_HZ));
	m_parameters[m_freqname].SetIntVal(100 * INT64_C(1000) * INT64_C(1000));

	m_parameters[m_biasname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_biasname].SetFloatVal(0);

	m_parameters[m_amplitudename] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_amplitudename].SetFloatVal(1);

	m_parameters[m_depthname] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLEDEPTH));
	m_parameters[m_depthname].SetIntVal(100 * 1000);

	m_parameters[m_phasename] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_DEGREES));
	m_parameters[m_phasename].SetFloatVal(0);

	m_parameters[m_unitname] = FilterParameter::UnitSelector();
	m_parameters[m_unitname].SetIntVal(Unit::UNIT_VOLTS);
	m_parameters[m_unitname].signal_changed().connect(sigc::mem_fun(*this, &SawtoothGeneratorFilter::OnUnitChanged));

	m_parameters[m_rampname] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));;
	m_parameters[m_rampname].AddEnumValue("Up", RAMP_UP);
	m_parameters[m_rampname].AddEnumValue("Down", RAMP_DOWN);
	m_parameters[m_rampname].AddEnumValue("Both", RAMP_BOTH);
	m_parameters[m_rampname].SetIntVal(RAMP_UP);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool SawtoothGeneratorFilter::ValidateChannel(size_t /*i*/, StreamDescriptor /*stream*/)
{
	//no inputs
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string SawtoothGeneratorFilter::GetProtocolName()
{
	return "Sawtooth";
}

void SawtoothGeneratorFilter::OnUnitChanged()
{
	Unit unit(static_cast<Unit::UnitType>(m_parameters[m_unitname].GetIntVal()));

	SetYAxisUnits(unit, 0);
	m_parameters[m_amplitudename].SetUnit(unit);
	m_parameters[m_biasname].SetUnit(unit);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void SawtoothGeneratorFilter::Refresh()
{
	int64_t samplerate = m_parameters[m_ratename].GetIntVal();
	int64_t freq = m_parameters[m_freqname].GetIntVal();
	size_t samplePeriod = FS_PER_SECOND / samplerate;
	float bias = m_parameters[m_biasname].GetFloatVal();
	float amplitude = m_parameters[m_amplitudename].GetFloatVal();
	size_t depth = m_parameters[m_depthname].GetIntVal();
	float startphase_deg = m_parameters[m_phasename].GetFloatVal();
	float startphase_frac = startphase_deg / 360;

	double t = GetTime();
	int64_t fs = (t - floor(t)) * FS_PER_SECOND;

	auto cap = dynamic_cast<UniformAnalogWaveform*>(GetData(0));
	if(!cap)
	{
		cap = new UniformAnalogWaveform;
		SetData(cap, 0);
	}
	cap->m_timescale = samplePeriod;
	cap->m_triggerPhase = 0;
	cap->m_startTimestamp = floor(t);
	cap->m_startFemtoseconds = fs;
	cap->Resize(depth);
	cap->PrepareForCpuAccess();

	double samples_per_cycle = samplerate * 1.0 / freq;
	double cycles_per_sample = 1.0 / samples_per_cycle;
	float vmin = bias - amplitude/2;

	auto dir = m_parameters[m_rampname].GetIntVal();

	size_t i = 0;
	switch(dir)
	{
		case RAMP_UP:
			for(i=0; i<depth; i++)
				cap->m_samples[i] = vmin + amplitude*fmodf(i*cycles_per_sample + startphase_frac, 1);
			break;

		case RAMP_DOWN:
			for(i=0; i<depth; i++)
				cap->m_samples[i] = vmin + amplitude*(1 - fmodf(i*cycles_per_sample + startphase_frac, 1));
			break;

		case RAMP_BOTH:
			for(i=0; i<depth; i++)
			{
				float pos = fabs(1 - fmodf(i*(cycles_per_sample*2) + startphase_frac, 2));
				cap->m_samples[i] = vmin + amplitude*pos;
			}
			break;

		default:
			break;
	}

	cap->MarkModifiedFromCpu();
}
