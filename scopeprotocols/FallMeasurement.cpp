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
#include "../scopehal/KahanSummation.h"
#include "FallMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

FallMeasurement::FallMeasurement(const string& color)
	: Filter(color, CAT_MEASUREMENT)
	, m_start(m_parameters["Start Fraction"])
	, m_end(m_parameters["End Fraction"])
	, m_minmaxPipeline("shaders/MinMax.spv", 3, sizeof(uint32_t))
{
	//Set up channels
	CreateInput("din");
	AddStream(Unit(Unit::UNIT_FS), "trend", Stream::STREAM_TYPE_ANALOG);
	AddStream(Unit(Unit::UNIT_FS), "avg", Stream::STREAM_TYPE_ANALOG_SCALAR);

	m_start = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_PERCENT));
	m_start.SetFloatVal(0.8);

	m_end = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_PERCENT));
	m_end.SetFloatVal(0.2);

	if(g_hasShaderInt64)
	{
		m_firstPassUniformComputePipeline =
			make_shared<ComputePipeline>("shaders/FallMeasurement_Uniform.spv", 3, sizeof(FallPushConstants));

		m_finalPassComputePipeline =
			make_shared<ComputePipeline>("shaders/FallMeasurement_FinalPass.spv", 7, sizeof(FallPushConstants));

		m_firstPassOffsets.SetGpuAccessHint(AcceleratorBuffer<int64_t>::HINT_LIKELY);
		m_firstPassSamples.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);

		m_finalSampleCount.SetGpuAccessHint(AcceleratorBuffer<int64_t>::HINT_LIKELY);
		m_partialSums.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
	}

	if(g_hasShaderInt64 && g_hasShaderAtomicInt64)
	{
		m_histogramPipeline =
			make_shared<ComputePipeline>("shaders/Histogram.spv", 2, sizeof(HistogramConstants));

		m_histogramBuf.SetGpuAccessHint(AcceleratorBuffer<uint64_t>::HINT_LIKELY);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool FallMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string FallMeasurement::GetProtocolName()
{
	return "Fall";
}

Filter::DataLocation FallMeasurement::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void FallMeasurement::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("FallMeasurement::Refresh");
	#endif

	//Make sure we've got valid inputs
	ClearErrors();
	if(!VerifyAllInputsOK())
	{
		if(!GetInput(0))
			AddErrorMessage("Missing inputs", "No signal input connected");
		else if(!GetInputWaveform(0))
			AddErrorMessage("Missing inputs", "No waveform available at input");

		SetData(nullptr, 0);
		m_streams[1].m_value = NAN;
		return;
	}

	//Get the input data
	auto din = GetInputWaveform(0);
	auto sdin = dynamic_cast<SparseAnalogWaveform*>(din);
	auto udin = dynamic_cast<UniformAnalogWaveform*>(din);
	size_t len = din->size();

	//Create the output
	auto cap = SetupEmptySparseAnalogOutputWaveform(din, 0, true);
	cap->m_timescale = 1;

	//Get the base/top (we use these for calculating percentages)
	float base;
	float top;
	GetBaseAndTopVoltage(
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

	//GPU path
	//TODO: GPU sparse path
	if(g_hasShaderInt64 && udin)
	{
		const uint32_t nthreads = 8192;

		cmdBuf.begin({});

		//Worst case, we have falling and rising edges alternating
		//so we can have at most half the input length at the output
		uint32_t maxFallingEdges = len/2;
		uint32_t bufferPerThread = GetComputeBlockCount(maxFallingEdges, nthreads) + 1;

		FallPushConstants cfg;
		cfg.timescale = din->m_timescale;
		cfg.len = len;
		cfg.bufferPerThread = bufferPerThread;
		cfg.vstart = vstart;
		cfg.vend = vend;

		//Resize scratch buffers assuming we have (at most) one sample per two input edges
		//(Offset buffer also needs one entry per thread for size output)
		//We can get away with slightly less sample buffer but this keeps indexing math simple
		m_firstPassOffsets.resize(bufferPerThread * nthreads);
		m_firstPassSamples.resize(bufferPerThread * nthreads);

		//First pass: look for edges in each block
		m_firstPassUniformComputePipeline->BindBufferNonblocking(0, udin->m_samples, cmdBuf);
		m_firstPassUniformComputePipeline->BindBufferNonblocking(1, m_firstPassOffsets, cmdBuf, true);
		m_firstPassUniformComputePipeline->BindBufferNonblocking(2, m_firstPassSamples, cmdBuf, true);
		m_firstPassUniformComputePipeline->Dispatch(cmdBuf, cfg, GetComputeBlockCount(nthreads, 64));
		m_firstPassUniformComputePipeline->AddComputeMemoryBarrier(cmdBuf);

		m_firstPassOffsets.MarkModifiedFromGpu();
		m_firstPassSamples.MarkModifiedFromGpu();

		//Second pass: coalesce outputs into one
		cap->Resize(maxFallingEdges);
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

		double sum = 0;
		for(size_t i=0; i<nthreads; i++)
			sum += m_partialSums[i];
		m_streams[1].m_value = sum / cap->m_samples.size();
	}

	//CPU fallback
	else
	{
		din->PrepareForCpuAccess();
		cap->PrepareForCpuAccess();

		float last = -1e20;
		int64_t tedge = 0;

		int state = 0;
		int64_t tlast = 0;

		//LogDebug("vstart = %.3f, vend = %.3f\n", vstart, vend);
		KahanSummation sum;
		int64_t num = 0;

		//Sparse path
		if(sdin)
		{
			for(size_t i=0; i < len; i++)
			{
				float cur = sdin->m_samples[i];
				int64_t tnow = sdin->m_offsets[i] * din->m_timescale;

				//Find start of edge
				if(state == 0)
				{
					if( (cur < vstart) && (last >= vstart) )
					{
						int64_t xdelta = InterpolateTime(sdin, i-1, vstart) * din->m_timescale;
						tedge = tnow - din->m_timescale + xdelta;
						state = 1;
					}
				}

				//Find end of edge
				else if(state == 1)
				{
					if( (cur < vend) && (last >= vend) )
					{
						int64_t xdelta = InterpolateTime(sdin, i-1, vend) * din->m_timescale;
						int64_t dt = xdelta + tnow - din->m_timescale - tedge;

						auto outlen = cap->m_offsets.size();
						if(outlen)
							cap->m_durations[outlen-1] = tnow - tlast;

						cap->m_offsets.push_back(tnow);
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
		}

		//Uniform path
		else
		{
			for(size_t i=0; i < len; i++)
			{
				float cur = udin->m_samples[i];
				int64_t tnow = i * din->m_timescale;

				//Find start of edge
				if(state == 0)
				{
					if( (cur < vstart) && (last >= vstart) )
					{
						int64_t xdelta = InterpolateTime(udin, i-1, vstart) * din->m_timescale;
						tedge = tnow - din->m_timescale + xdelta;
						state = 1;
					}
				}

				//Find end of edge
				else if(state == 1)
				{
					if( (cur < vend) && (last >= vend) )
					{
						int64_t xdelta = InterpolateTime(udin, i-1, vend) * din->m_timescale;
						int64_t dt = xdelta + tnow - din->m_timescale - tedge;

						auto outlen = cap->m_offsets.size();
						if(outlen)
							cap->m_durations[outlen-1] = tnow - tlast;

						cap->m_offsets.push_back(tnow);
						cap->m_durations.push_back(1);
						cap->m_samples.push_back(dt);
						tlast = tnow;

						sum += dt;
						num ++;

						state = 0;
					}
				}

				last = cur;
			}
		}

		cap->MarkModifiedFromCpu();
			m_streams[1].m_value = sum.GetSum() / num;
	}
}
