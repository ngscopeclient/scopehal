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
#include "ToneGeneratorFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ToneGeneratorFilter::ToneGeneratorFilter(const string& color)
	: Filter(color, CAT_GENERATION)
	, m_rate(m_parameters["Sample Rate"])
	, m_freq(m_parameters["Frequency"])
	, m_bias(m_parameters["DC Bias"])
	, m_amplitude(m_parameters["Amplitude"])
	, m_depth(m_parameters["Depth"])
	, m_phase(m_parameters["Starting Phase"])
	, m_unit(m_parameters["Unit"])
	, m_computePipeline("shaders/ToneGeneratorFilter.spv", 1, sizeof(ToneGeneratorConstants))
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);

	m_rate = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLERATE));
	m_rate.SetIntVal(100 * INT64_C(1000) * INT64_C(1000) * INT64_C(1000));

	m_freq = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_HZ));
	m_freq.SetIntVal(100 * INT64_C(1000) * INT64_C(1000));

	m_bias = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_bias.SetFloatVal(0);

	m_amplitude = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_amplitude.SetFloatVal(1);

	m_depth = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLEDEPTH));
	m_depth.SetIntVal(100 * 1000);

	m_phase = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_DEGREES));
	m_phase.SetFloatVal(0);

	m_unit = FilterParameter::UnitSelector();
	m_unit.SetIntVal(Unit::UNIT_VOLTS);
	m_unit.signal_changed().connect(sigc::mem_fun(*this, &ToneGeneratorFilter::OnUnitChanged));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool ToneGeneratorFilter::ValidateChannel(
	[[maybe_unused]] size_t i,
	[[maybe_unused]] StreamDescriptor stream)
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

void ToneGeneratorFilter::OnUnitChanged()
{
	Unit unit(static_cast<Unit::UnitType>(m_unit.GetIntVal()));

	SetYAxisUnits(unit, 0);
	m_amplitude.SetUnit(unit);
	m_bias.SetUnit(unit);
}

Filter::DataLocation ToneGeneratorFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void ToneGeneratorFilter::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("ToneGeneratorFilter::Refresh");
	#endif

	int64_t samplerate = m_rate.GetIntVal();
	int64_t freq = m_freq.GetIntVal();
	size_t samplePeriod = FS_PER_SECOND / samplerate;
	float bias = m_bias.GetFloatVal();
	float amplitude = m_amplitude.GetFloatVal();
	size_t depth = m_depth.GetIntVal();
	float startphase_deg = m_phase.GetFloatVal();
	float startphase = startphase_deg * 2 * M_PI / 360;

	double t = GetTime();
	int64_t fs = (t - floor(t)) * FS_PER_SECOND;

	auto cap = SetupEmptyUniformAnalogOutputWaveform(nullptr, 0);
	cap->m_timescale = samplePeriod;
	cap->m_triggerPhase = 0;
	cap->m_startTimestamp = floor(t);
	cap->m_startFemtoseconds = fs;
	cap->Resize(depth);

	double samples_per_cycle = samplerate * 1.0 / freq;
	double radians_per_sample = 2 * M_PI / samples_per_cycle;

	//sin is +/- 1, so need to divide amplitude by 2 to get scaling factor
	float scale = amplitude / 2;

	//Push onstants
	ToneGeneratorConstants cfg;
	cfg.radians_per_sample = radians_per_sample;
	cfg.depth = depth;
	cfg.bias = bias;
	cfg.scale = scale;
	cfg.startphase = startphase;

	//Run the main shader
	cmdBuf.begin({});
	m_computePipeline.BindBufferNonblocking(0, cap->m_samples, cmdBuf, true);

	const uint32_t compute_block_count = GetComputeBlockCount(depth, 64);
	m_computePipeline.Dispatch(cmdBuf, cfg,
		min(compute_block_count, 32768u),
		compute_block_count / 32768 + 1);

	cap->MarkModifiedFromGpu();

	cmdBuf.end();
	queue->SubmitAndBlock(cmdBuf);
}
