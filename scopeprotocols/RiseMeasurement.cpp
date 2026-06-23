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
#include "RiseMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

RiseMeasurement::RiseMeasurement(const string& color)
	: Filter(color, CAT_MEASUREMENT)
	, m_start(m_parameters["Start Fraction"])
	, m_end(m_parameters["End Fraction"])
	, m_minmaxPipeline("shaders/MinMax.spv", 3, sizeof(uint32_t))
{
	AddStream(Unit(Unit::UNIT_FS), "trend", Stream::STREAM_TYPE_ANALOG);
	AddStream(Unit(Unit::UNIT_FS), "avg", Stream::STREAM_TYPE_ANALOG_SCALAR);
	CreateInput<InputConstraintStreamType>("din", Stream::STREAM_TYPE_ANALOG);

	m_start = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_PERCENT));
	m_start.SetFloatVal(0.2);

	m_end = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_PERCENT));
	m_end.SetFloatVal(0.8);

	if(g_hasShaderInt64 && g_hasShaderAtomicInt64)
	{
		m_histogramPipeline =
			make_shared<ComputePipeline>("shaders/Histogram.spv", 2, sizeof(HistogramConstants));

		m_histogramBuf.SetGpuAccessHint(AcceleratorBuffer<uint64_t>::HINT_LIKELY);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void RiseMeasurement::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "Rise(%s, %s, %s)",
		GetInputDisplayName(0).c_str(),
		m_start.ToString().c_str(),
		m_end.ToString().c_str()
		);
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string RiseMeasurement::GetProtocolName()
{
	return "Rise";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void RiseMeasurement::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue
	)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("RiseMeasurement::Refresh");
	#endif
	ClearErrors();

	if(!VerifyAllInputsOK())
	{
		AddErrorMessage("Missing input", "One or more inputs are unconnected");
		SetData(nullptr, 0);
		m_streams[1].m_value = 0;
		return;
	}

	//Get the input data
	auto din = GetInputWaveform(0);
	size_t len = din->size();
	din->PrepareForCpuAccess();
	auto sdin = dynamic_cast<SparseAnalogWaveform*>(din);
	auto udin = dynamic_cast<UniformAnalogWaveform*>(din);

	//Get the base/top (we use these for calculating percentages)
	float base;
	float top;
	Filter::GetBaseAndTopVoltage(
		cmdBuf,
		queue,
		m_minmaxPipeline,
		m_histogramPipeline,
		m_minbuf,
		m_maxbuf,
		m_histogramBuf,
		sdin,
		udin,
		base,
		top);

	//Find the actual levels we use for our time gate
	float delta = top - base;
	float vstart = base + m_start.GetFloatVal()*delta;
	float vend = base + m_end.GetFloatVal()*delta;

	//Create the output
	auto cap = SetupEmptySparseAnalogOutputWaveform(din, 0);
	cap->m_timescale = 1;
	cap->PrepareForCpuAccess();

	float last = 1e20;
	int64_t tedgeInt = 0;
	float tedgeFrac = 0;

	int state = 0;
	int64_t tlast = 0;

	//LogDebug("vstart = %.3f, vend = %.3f\n", vstart, vend);
	KahanSummation sum;
	int64_t num = 0;
	for(size_t i=0; i < len; i++)
	{
		float cur = GetValue(sdin, udin, i);
		int64_t tnow = ::GetOffsetScaled(sdin, udin, i);

		//Find start of edge
		if(state == 0)
		{
			if( (cur > vstart) && (last <= vstart) )
			{
				tedgeInt = tnow - din->m_timescale;
				tedgeFrac = InterpolateTime(sdin, udin, i-1, vstart)*din->m_timescale;
				state = 1;
			}
		}

		//Find end of edge
		else if(state == 1)
		{
			if( (cur > vend) && (last <= vend) )
			{
				int64_t dtInt = (tnow - din->m_timescale) - tedgeInt;
				float dtFrac = InterpolateTime(sdin, udin, i-1, vend)*din->m_timescale - tedgeFrac;
				float dt = dtInt + dtFrac;

				cap->m_offsets.push_back(tlast);
				cap->m_durations.push_back(tnow - tlast);
				cap->m_samples.push_back(dt);
				tlast = tnow;

				sum += dt;
				num ++;

				state = 0;
			}
		}

		last = cur;
	}

	SetData(cap, 0);
	cap->MarkModifiedFromCpu();

	m_streams[1].m_value = sum.GetSum() / num;
}
