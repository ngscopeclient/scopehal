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
#include "BaseMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

BaseMeasurement::BaseMeasurement(const string& color)
	: Filter(color, CAT_MEASUREMENT)
	, m_minmaxPipeline("shaders/MinMax.spv", 3, sizeof(uint32_t))
{
	AddStream(Unit(Unit::UNIT_VOLTS), "trend", Stream::STREAM_TYPE_ANALOG);
	AddStream(Unit(Unit::UNIT_VOLTS), "avg", Stream::STREAM_TYPE_ANALOG_SCALAR);

	CreateInput("din");

	if(g_hasShaderInt64 && g_hasShaderAtomicInt64)
	{
		m_histogramPipeline =
			make_shared<ComputePipeline>("shaders/Histogram.spv", 2, sizeof(HistogramConstants));

		m_histogramBuf.SetGpuAccessHint(AcceleratorBuffer<uint64_t>::HINT_LIKELY);
	}

	if(g_hasShaderInt64)
	{
		m_firstPassComputePipeline =
			make_shared<ComputePipeline>("shaders/BaseMeasurement_FirstPass.spv", 3, sizeof(BasePushConstants));
		m_finalPassComputePipeline =
			make_shared<ComputePipeline>("shaders/BaseMeasurement_FinalPass.spv", 7, sizeof(BasePushConstants));

		m_firstPassOffsets.SetGpuAccessHint(AcceleratorBuffer<int64_t>::HINT_LIKELY);
		m_firstPassSamples.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);

		m_finalSampleCount.SetGpuAccessHint(AcceleratorBuffer<int64_t>::HINT_LIKELY);
		m_partialSums.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool BaseMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string BaseMeasurement::GetProtocolName()
{
	return "Base";
}

Filter::DataLocation BaseMeasurement::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void BaseMeasurement::Refresh(
	vk::raii::CommandBuffer& cmdBuf,
	std::shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("BaseMeasurement::Refresh");
	#endif

	ClearErrors();

	//Set up input
	auto in = GetInput(0).GetData();
	auto uin = dynamic_cast<UniformAnalogWaveform*>(in);
	auto sin = dynamic_cast<SparseAnalogWaveform*>(in);
	if(!uin && !sin)
	{
		if(!GetInput(0))
			AddErrorMessage("Missing inputs", "No signal input connected");
		else if(!GetInputWaveform(0))
			AddErrorMessage("Missing inputs", "No waveform available at input");

		SetData(nullptr, 0);
		return;
	}
	size_t len = in->size();

	//Copy input unit to output
	SetYAxisUnits(m_inputs[0].GetYAxisUnits(), 0);
	SetYAxisUnits(m_inputs[0].GetYAxisUnits(), 1);

	//Make a histogram of the waveform
	float vmin;
	float vmax;
	if(sin)
		GetMinMaxVoltage(cmdBuf, queue, m_minmaxPipeline, m_minbuf, m_maxbuf, sin, vmin, vmax);
	else
		GetMinMaxVoltage(cmdBuf, queue, m_minmaxPipeline, m_minbuf, m_maxbuf, uin, vmin, vmax);

	//GPU side histogram calculation
	size_t nbins = 128;
	if(g_hasShaderInt64 && g_hasShaderAtomicInt64)
	{
		if(sin)
			MakeHistogram(cmdBuf, queue, *m_histogramPipeline, sin, m_histogramBuf, vmin, vmax, nbins);
		else
			MakeHistogram(cmdBuf, queue, *m_histogramPipeline, uin, m_histogramBuf, vmin, vmax, nbins);
	}

	//CPU fallback
	else
	{
		PrepareForCpuAccess(sin, uin);
		m_histogramBuf.PrepareForCpuAccess();

		auto hist = MakeHistogram(sin, uin, vmin, vmax, nbins);
		for(size_t i=0; i<nbins; i++)
			m_histogramBuf[i] = hist[i];

		m_histogramBuf.MarkModifiedFromCpu();
	}

	m_histogramBuf.PrepareForCpuAccess();

	//Find the highest peak in the first quarter of the histogram
	//This is the expected base for the entire waveform
	size_t binval = 0;
	size_t idx = 0;
	for(size_t i=0; i<(nbins/4); i++)
	{
		if(m_histogramBuf[i] > binval)
		{
			binval = m_histogramBuf[i];
			idx = i;
		}
	}
	float fbin = (idx + 0.5f)/nbins;

	//Create the output
	auto cap = SetupEmptySparseAnalogOutputWaveform(in, 0, true);
	cap->m_timescale = 1;

	//GPU side inner loop
	//TODO: support sparse
	const uint32_t nthreads = 4096;
	if(g_hasShaderInt64 && uin)
	{
		float range = (vmax - vmin);

		BasePushConstants cfg;
		cfg.len = len;
		cfg.vmin = vmin;
		cfg.mid = range/2 + vmin;
		cfg.global_base = fbin*range + vmin;
		cfg.bufferPerThread = GetComputeBlockCount(len/2, nthreads) + 1;
		cfg.timescale = in->m_timescale;
		cfg.triggerPhase = in->m_triggerPhase;
		cfg.range = range;

		cmdBuf.begin({});

		//Resize scratch buffers assuming we have (at most) one sample per two input edges
		//(Offset buffer also needs one entry per thread for size output)
		//We can get away with slightly less sample buffer but this keeps indexing math simple
		m_firstPassOffsets.resize(cfg.bufferPerThread * nthreads);
		m_firstPassSamples.resize(cfg.bufferPerThread * nthreads);

		//First pass: look for edges in each block
		m_firstPassComputePipeline->BindBufferNonblocking(0, uin->m_samples, cmdBuf);
		m_firstPassComputePipeline->BindBufferNonblocking(1, m_firstPassOffsets, cmdBuf, true);
		m_firstPassComputePipeline->BindBufferNonblocking(2, m_firstPassSamples, cmdBuf, true);
		m_firstPassComputePipeline->Dispatch(cmdBuf, cfg, GetComputeBlockCount(nthreads, 64));
		m_firstPassComputePipeline->AddComputeMemoryBarrier(cmdBuf);

		m_firstPassOffsets.MarkModifiedFromGpu();
		m_firstPassSamples.MarkModifiedFromGpu();

		//Second pass: coalesce outputs into one
		cap->Resize(len/2);
		m_finalSampleCount.resize(1);
		m_partialSums.resize(nthreads);

		m_finalPassComputePipeline->BindBufferNonblocking(0, m_firstPassOffsets, cmdBuf);
		m_finalPassComputePipeline->BindBufferNonblocking(1, m_firstPassSamples, cmdBuf);
		m_finalPassComputePipeline->BindBufferNonblocking(2, cap->m_offsets, cmdBuf, true);
		m_finalPassComputePipeline->BindBufferNonblocking(3, cap->m_samples, cmdBuf, true);
		m_finalPassComputePipeline->BindBufferNonblocking(4, cap->m_durations, cmdBuf, true);
		m_finalPassComputePipeline->BindBufferNonblocking(5, m_finalSampleCount, cmdBuf, true);
		m_finalPassComputePipeline->BindBufferNonblocking(6, m_partialSums, cmdBuf, true);
		m_finalPassComputePipeline->Dispatch(cmdBuf, cfg, GetComputeBlockCount(nthreads, 64));
		m_finalPassComputePipeline->AddComputeMemoryBarrier(cmdBuf);

		cap->MarkModifiedFromGpu();
		m_finalSampleCount.MarkModifiedFromGpu();
		m_partialSums.MarkModifiedFromGpu();

		m_finalSampleCount.PrepareForCpuAccessNonblocking(cmdBuf);
		m_partialSums.PrepareForCpuAccessNonblocking(cmdBuf);

		//Done
		cmdBuf.end();
		queue->SubmitAndBlock(cmdBuf);

		//Update size
		cap->Resize(m_finalSampleCount[0]);
	}

	//CPU side inner loop
	else if(sin)
		InnerLoop(sin, cap, len, vmin, vmax, fbin);
	else
		InnerLoop(uin, cap, len, vmin, vmax, fbin);

	//GPU average postprocessing
	if(g_hasShaderInt64 && uin)
	{
		double sum = 0;
		for(size_t i=0; i<nthreads; i++)
			sum += m_partialSums[i];
		m_streams[1].m_value = sum / cap->m_samples.size();
	}

	else
	{
		//TODO: do this GPU side too
		cap->PrepareForCpuAccess();
		double sum = 0;
		for(auto f : cap->m_samples)
			sum += f;
		m_streams[1].m_value = sum / cap->m_samples.size();
	}
}
