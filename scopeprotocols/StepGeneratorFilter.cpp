/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
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
	: Filter(color, CAT_GENERATION)
	, m_low(m_parameters["Beginning Level"])
	, m_high(m_parameters["Ending Level"])
	, m_rate(m_parameters["Sample Rate"])
	, m_depth(m_parameters["Memory Depth"])
	, m_steptime(m_parameters["Step Position"])
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);

	m_low = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_low.SetFloatVal(0);

	m_high = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_high.SetFloatVal(1);

	m_rate = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLERATE));
	m_rate.SetIntVal(500 * INT64_C(1000) * INT64_C(1000) * INT64_C(1000));

	m_depth = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLEDEPTH));
	m_depth.SetIntVal(100 * 1000);

	m_steptime = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLEDEPTH));
	m_steptime.SetIntVal(50 * 1000);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string StepGeneratorFilter::GetProtocolName()
{
	return "Step";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void StepGeneratorFilter::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue
	)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("StepGeneratorFilter::Refresh");
	#endif
	ClearErrors();

	int64_t samplerate = m_rate.GetIntVal();
	size_t samplePeriod = FS_PER_SECOND / samplerate;
	size_t depth = m_depth.GetIntVal();
	size_t mid = m_steptime.GetIntVal();
	float vstart = m_low.GetFloatVal();
	float vend = m_high.GetFloatVal();

	double t = GetTime();
	int64_t fs = (t - floor(t)) * FS_PER_SECOND;

	auto cap = SetupEmptyUniformAnalogOutputWaveform(nullptr, 0);
	cap->PrepareForCpuAccess();
	cap->m_timescale = samplePeriod;
	cap->m_triggerPhase = 0;
	cap->m_startTimestamp = floor(t);
	cap->m_startFemtoseconds = fs;
	cap->Resize(depth);

	for(size_t i=0; i<depth; i++)
	{
		if(i < mid)
			cap->m_samples[i] = vstart;
		else
			cap->m_samples[i] = vend;
	}

	cap->MarkModifiedFromCpu();
}
