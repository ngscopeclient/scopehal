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
#include "IQDemuxFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

IQDemuxFilter::IQDemuxFilter(const string& color)
	: Filter(color, CAT_RF)
	, m_alignment(m_parameters["Alignment"])
{
	AddStream(Unit(Unit::UNIT_VOLTS), "I", Stream::STREAM_TYPE_ANALOG);
	AddStream(Unit(Unit::UNIT_VOLTS), "Q", Stream::STREAM_TYPE_ANALOG);

	CreateInput("sampledData");

	m_alignment = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_alignment.AddEnumValue("None", ALIGN_NONE);
	m_alignment.AddEnumValue("100Base-T1", ALIGN_100BASET1);
	m_alignment.SetIntVal(ALIGN_NONE);

	if(g_hasShaderInt64)
	{
		m_demuxComputePipeline =
			make_shared<ComputePipeline>("shaders/IQDemuxFilter.spv", 8, sizeof(IQDemuxConstants));
	}

	m_alignComputePipeline =
		make_shared<ComputePipeline>("shaders/IQDemuxFilterAlignment.spv", 2, sizeof(uint32_t));
	m_alignOut.resize(2);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool IQDemuxFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string IQDemuxFilter::GetProtocolName()
{
	return "IQ Demux";
}

Filter::DataLocation IQDemuxFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void IQDemuxFilter::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("IQDemuxFilter::Refresh");
	#endif

	auto din = dynamic_cast<SparseAnalogWaveform*>(GetInputWaveform(0));
	ClearErrors();
	if(!din)
	{
		if(!GetInput(0))
			AddErrorMessage("Missing inputs", "No signal input connected");
		else if(!GetInputWaveform(0))
			AddErrorMessage("Missing inputs", "No waveform available at input");
		else
			AddErrorMessage("Invalid inputs", "Expect a sparse analog waveform");

		SetData(nullptr, 0);
		SetData(nullptr, 1);
		return;
	}

	size_t len = din->m_samples.size();
	LogTrace("%zu sampled data points\n", len);

	//Figure out the proper I-vs-Q alignment (even/odd is not specified)
	auto align = static_cast<AlignmentType>(m_alignment.GetIntVal());
	size_t istart = 0;

	if(align == ALIGN_100BASET1)
	{
		//We need to do this on the GPU even if it's not super time consuming, because it avoids a round trip
		//This doesn't depend on int64 so can be done on any GPU, just normal float32 and int32 operations

		//Look at a fixed window in the start of the waveform and see which one has the least (0,0) symbols
		size_t window = min(len, (size_t)10000);

		//Do the alignment check on the GPU
		cmdBuf.begin({});

		m_alignComputePipeline->BindBufferNonblocking(0, din->m_samples, cmdBuf);
		m_alignComputePipeline->BindBufferNonblocking(1, m_alignOut, cmdBuf, true);
		m_alignComputePipeline->Dispatch(cmdBuf, (uint32_t)window, 2);
		m_alignComputePipeline->AddComputeMemoryBarrier(cmdBuf);
		m_alignOut.PrepareForCpuAccessNonblocking(cmdBuf);

		cmdBuf.end();
		queue->SubmitAndBlock(cmdBuf);

		LogTrace("Phase 0: zeros = %u\n", m_alignOut[0]);
		LogTrace("Phase 1: zeros = %u\n", m_alignOut[1]);

		if(m_alignOut[0] < m_alignOut[1])
			istart = 0;
		else
			istart = 1;
	}

	//Make output waveforms
	auto iout = SetupEmptySparseAnalogOutputWaveform(din, 0);
	auto qout = SetupEmptySparseAnalogOutputWaveform(din, 1);
	size_t outlen = (len - istart) / 2;
	iout->Resize(outlen);
	qout->Resize(outlen);

	if(g_hasShaderInt64)
	{
		cmdBuf.begin({});

		IQDemuxConstants cfg;
		cfg.istart = istart;
		cfg.outlen = outlen;

		uint64_t numThreads = outlen;
		const uint64_t blockSize = 64;
		const uint64_t numBlocks = GetComputeBlockCount(numThreads, blockSize);

		//Do the demux
		m_demuxComputePipeline->BindBufferNonblocking(0, din->m_samples, cmdBuf);
		m_demuxComputePipeline->BindBufferNonblocking(1, din->m_offsets, cmdBuf);
		m_demuxComputePipeline->BindBufferNonblocking(2, iout->m_samples, cmdBuf, true);
		m_demuxComputePipeline->BindBufferNonblocking(3, iout->m_offsets, cmdBuf, true);
		m_demuxComputePipeline->BindBufferNonblocking(4, iout->m_durations, cmdBuf, true);
		m_demuxComputePipeline->BindBufferNonblocking(5, qout->m_samples, cmdBuf, true);
		m_demuxComputePipeline->BindBufferNonblocking(6, qout->m_offsets, cmdBuf, true);
		m_demuxComputePipeline->BindBufferNonblocking(7, qout->m_durations, cmdBuf, true);
		m_demuxComputePipeline->Dispatch(cmdBuf, cfg, numBlocks);

		iout->MarkModifiedFromGpu();
		qout->MarkModifiedFromGpu();

		cmdBuf.end();
		queue->SubmitAndBlock(cmdBuf);
	}

	else
	{
		iout->PrepareForCpuAccess();
		qout->PrepareForCpuAccess();

		//Synthesize the output
		bool clkval = false;
		size_t nout = 0;
		for(size_t i=istart; i+1 < len; i += 2)
		{
			int64_t tnow = din->m_offsets[i];

			//Extend previous sample, if any
			if(nout)
			{
				int64_t dur = tnow - iout->m_offsets[nout-1];
				iout->m_durations[nout-1] = dur;
				qout->m_durations[nout-1] = dur;
			}

			//Add this sample
			iout->m_offsets[nout] = tnow;
			qout->m_offsets[nout] = tnow;

			iout->m_durations[nout] = 1;
			qout->m_durations[nout] = 1;

			iout->m_samples[nout] = din->m_samples[i];
			qout->m_samples[nout] = din->m_samples[i+1];

			clkval = !clkval;

			nout ++;
		}

		iout->MarkModifiedFromCpu();
		qout->MarkModifiedFromCpu();
	}
}
