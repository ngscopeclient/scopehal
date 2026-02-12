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
#include "MovingAverageFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

MovingAverageFilter::MovingAverageFilter(const string& color)
	: Filter(color, CAT_MATH)
	, m_depth(m_parameters["Depth"])
	, m_uniformComputePipeline("shaders/MovingAverageFilter_Uniform.spv", 2, sizeof(MovingAveragePushConstants))
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput("din");

	m_depth = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLEDEPTH));
	m_depth.SetFloatVal(10);

	if(g_hasShaderInt64)
	{
		m_sparseComputePipeline = make_unique<ComputePipeline>(
			"shaders/MovingAverageFilter_Sparse.spv", 5, sizeof(MovingAveragePushConstants));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool MovingAverageFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string MovingAverageFilter::GetProtocolName()
{
	return "Moving average";
}

Filter::DataLocation MovingAverageFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void MovingAverageFilter::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("MovingAverageFilter::Refresh");
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
		return;
	}

	//Get the input data
	auto din = GetInputWaveform(0);
	size_t len = din->size();
	size_t depth = m_depth.GetIntVal();
	if(len < depth)
	{
		AddErrorMessage("Input too short", "Input signal must be larger than the averaging window");

		SetData(nullptr, 0);
		return;
	}

	m_xAxisUnit = m_inputs[0].m_channel->GetXAxisUnits();
	SetYAxisUnits(m_inputs[0].GetYAxisUnits(), 0);

	size_t off = depth/2;
	size_t nsamples = len - 2*off;

	auto sdin = dynamic_cast<SparseAnalogWaveform*>(din);
	auto udin = dynamic_cast<UniformAnalogWaveform*>(din);

	//Push constants
	MovingAveragePushConstants cfg;
	cfg.nsamples = nsamples;
	cfg.depth = depth;
	cfg.scale = 1.0 / depth;

	//Sparse path
	if(sdin)
	{
		auto cap = SetupEmptySparseAnalogOutputWaveform(sdin, 0);
		cap->Resize(nsamples);

		//GPU version if we have native int64 support
		if(g_hasShaderInt64)
		{
			cmdBuf.begin({});

			m_sparseComputePipeline->BindBufferNonblocking(0, sdin->m_samples, cmdBuf);
			m_sparseComputePipeline->BindBufferNonblocking(1, sdin->m_offsets, cmdBuf);
			m_sparseComputePipeline->BindBufferNonblocking(2, cap->m_samples, cmdBuf, true);
			m_sparseComputePipeline->BindBufferNonblocking(3, cap->m_offsets, cmdBuf, true);
			m_sparseComputePipeline->BindBufferNonblocking(4, cap->m_durations, cmdBuf, true);
			cap->MarkModifiedFromGpu();

			const uint32_t compute_block_count = GetComputeBlockCount(nsamples, 64);
			m_sparseComputePipeline->Dispatch(cmdBuf, cfg,
				min(compute_block_count, 32768u),
				compute_block_count / 32768 + 1);

			cmdBuf.end();
			queue->SubmitAndBlock(cmdBuf);
		}

		//CPU fallback
		else
		{
			din->PrepareForCpuAccess();
			cap->PrepareForCpuAccess();

			for(size_t i=0; i<nsamples; i++)
			{
				float v = 0;
				for(size_t j=0; j<depth; j++)
					v += sdin->m_samples[i+j];
				v /= depth;

				cap->m_offsets[i] = sdin->m_offsets[i+off];
				cap->m_durations[i] = sdin->m_durations[i+off];
				cap->m_samples[i] = v;
			}
			SetData(cap, 0);

			cap->MarkModifiedFromCpu();
		}
	}

	//Uniform path
	else
	{
		auto cap = SetupEmptyUniformAnalogOutputWaveform(udin, 0);
		cap->Resize(nsamples);

		//Phase shift by half the waveform length
		cap->m_triggerPhase = off * udin->m_timescale;

		cmdBuf.begin({});

		m_uniformComputePipeline.BindBufferNonblocking(0, udin->m_samples, cmdBuf);
		m_uniformComputePipeline.BindBufferNonblocking(1, cap->m_samples, cmdBuf, true);
		cap->MarkSamplesModifiedFromGpu();

		const uint32_t compute_block_count = GetComputeBlockCount(nsamples, 64);
		m_uniformComputePipeline.Dispatch(cmdBuf, cfg,
			min(compute_block_count, 32768u),
			compute_block_count / 32768 + 1);

		cmdBuf.end();
		queue->SubmitAndBlock(cmdBuf);
	}
}
