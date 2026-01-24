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
#include "DownconvertFilter.h"
#ifdef __x86_64__
#include <immintrin.h>
#include "avx_mathfun.h"
#endif

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DownconvertFilter::DownconvertFilter(const string& color)
	: Filter(color, CAT_RF)
	, m_freq(m_parameters["LO Frequency"])
	, m_computePipeline("shaders/Downconvert.spv", 3, sizeof(DownconvertConstants))
{
	//Set up channels
	CreateInput("RF");
	AddStream(Unit(Unit::UNIT_VOLTS), "I", Stream::STREAM_TYPE_ANALOG);
	AddStream(Unit(Unit::UNIT_VOLTS), "Q", Stream::STREAM_TYPE_ANALOG);

	//Optional input for LO frequency (overrides parameter)
	CreateInput("LOFrequency");

	m_freq = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ));
	m_freq.SetFloatVal(1e9);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DownconvertFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;
	if( (i == 1) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG_SCALAR) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string DownconvertFilter::GetProtocolName()
{
	return "Downconvert";
}

Filter::DataLocation DownconvertFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DownconvertFilter::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("DownconvertFilter::Refresh");
	#endif

	//Get the input data
	auto din = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(0));
	if(!din)
	{
		SetData(nullptr, 0);
		SetData(nullptr, 1);
		return;
	}
	din->PrepareForCpuAccess();

	//Get LO frequency
	//(input channel overrides parameter)
	double lo_freq = m_freq.GetFloatVal();
	auto loin = GetInput(1);
	if(loin)
		lo_freq = loin.GetScalarValue();

	//Set up output weaveforms
	auto cap_i = SetupEmptyUniformAnalogOutputWaveform(din, 0);
	auto cap_q = SetupEmptyUniformAnalogOutputWaveform(din, 1);
	size_t len = din->size();
	cap_i->Resize(len);
	cap_q->Resize(len);

	//Calculate phase velocity
	double lo_cycles_per_sample = (lo_freq * din->m_timescale) / FS_PER_SECOND;
	double lo_rad_per_sample = lo_cycles_per_sample * 2 * M_PI;
	DownconvertConstants cfg;
	cfg.size = len;
	cfg.trigger_phase_rad = din->m_triggerPhase * (lo_rad_per_sample / din->m_timescale);
	cfg.lo_rad_per_sample = lo_rad_per_sample;

	cmdBuf.begin({});

	m_computePipeline.BindBufferNonblocking(0, din->m_samples, cmdBuf);
	m_computePipeline.BindBufferNonblocking(1, cap_i->m_samples, cmdBuf, true);
	m_computePipeline.BindBufferNonblocking(2, cap_q->m_samples, cmdBuf, true);

	const uint32_t compute_block_count = GetComputeBlockCount(len, 64);
	m_computePipeline.Dispatch(cmdBuf, cfg,
		min(compute_block_count, 32768u),
		compute_block_count / 32768 + 1);

	cap_i->MarkSamplesModifiedFromGpu();
	cap_q->MarkSamplesModifiedFromGpu();

	cmdBuf.end();
	queue->SubmitAndBlock(cmdBuf);
}
