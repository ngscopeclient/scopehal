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
#include "StepGeneratorFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

StepGeneratorFilter::StepGeneratorFilter(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_GENERATION)
	, m_lowname("Beginning Level")
	, m_highname("Ending Level")
	, m_ratename("Sample Rate")
	, m_depthname("Memory Depth")
	, m_steptimename("Step Position")
{
	m_parameters[m_lowname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_lowname].SetFloatVal(0);

	m_parameters[m_highname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_highname].SetFloatVal(1);

	m_parameters[m_ratename] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLERATE));
	m_parameters[m_ratename].SetIntVal(500 * 1000L * 1000L * 1000L);

	m_parameters[m_depthname] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLEDEPTH));
	m_parameters[m_depthname].SetIntVal(100 * 1000);

	m_parameters[m_steptimename] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLEDEPTH));
	m_parameters[m_steptimename].SetIntVal(50 * 1000);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool StepGeneratorFilter::ValidateChannel(size_t /*i*/, StreamDescriptor /*stream*/)
{
	//no inputs
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string StepGeneratorFilter::GetProtocolName()
{
	return "Step";
}

float StepGeneratorFilter::GetVoltageRange(size_t /*stream*/)
{
	return fabs(m_parameters[m_lowname].GetFloatVal() - m_parameters[m_highname].GetFloatVal()) * 1.05;
}

float StepGeneratorFilter::GetOffset(size_t /*stream*/)
{
	return -(m_parameters[m_lowname].GetFloatVal() + m_parameters[m_highname].GetFloatVal()) / 2;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void StepGeneratorFilter::Refresh()
{
	int64_t samplerate = m_parameters[m_ratename].GetIntVal();
	size_t samplePeriod = FS_PER_SECOND / samplerate;
	size_t depth = m_parameters[m_depthname].GetIntVal();
	size_t mid = m_parameters[m_steptimename].GetIntVal();
	float vstart = m_parameters[m_lowname].GetFloatVal();
	float vend = m_parameters[m_highname].GetFloatVal();

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

	for(size_t i=0; i<depth; i++)
	{
		cap->m_offsets[i] = i;
		cap->m_durations[i] = 1;

		if(i < mid)
			cap->m_samples[i] = vstart;
		else
			cap->m_samples[i] = vend;
	}
}
