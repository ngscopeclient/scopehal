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
#include "ACCoupleFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ACCoupleFilter::ACCoupleFilter(const string& color)
	: Filter(color, CAT_MATH)
	, m_computePipeline("shaders/SubtractVectorScalar.spv", 2, sizeof(SubtractVectorScalarConstants))
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput("din");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool ACCoupleFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string ACCoupleFilter::GetProtocolName()
{
	return "AC Couple";
}

Filter::DataLocation ACCoupleFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void ACCoupleFilter::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("ACCoupleFilter::Refresh");
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

	auto data = GetInput(0).GetData();
	auto sdata = dynamic_cast<SparseAnalogWaveform*>(data);
	auto udata = dynamic_cast<UniformAnalogWaveform*>(data);

	//Find the average of our samples (assume data is DC balanced)
	float average;
	if(sdata)
		average = m_averager.Average(sdata, cmdBuf, queue);
	else
		average = m_averager.Average(udata, cmdBuf, queue);

	auto len = data->size();
	SubtractVectorScalarConstants cfg;
	cfg.offsetIn = 0;
	cfg.delta = average;
	cfg.size = len;

	cmdBuf.begin({});

	//Set up waveforms
	if(sdata)
	{
		auto cap = SetupSparseOutputWaveform(sdata, 0, 0, 0);
		m_computePipeline.BindBufferNonblocking(0, sdata->m_samples, cmdBuf);
		m_computePipeline.BindBufferNonblocking(1, cap->m_samples, cmdBuf, true);
		cap->MarkSamplesModifiedFromGpu();
	}
	else
	{
		auto cap = SetupEmptyUniformAnalogOutputWaveform(udata, 0);
		cap->Resize(len);
		m_computePipeline.BindBufferNonblocking(0, udata->m_samples, cmdBuf);
		m_computePipeline.BindBufferNonblocking(1, cap->m_samples, cmdBuf, true);
		cap->MarkSamplesModifiedFromGpu();
	}

	//Do the actual subtraction
	const uint32_t compute_block_count = GetComputeBlockCount(len, 64);
	m_computePipeline.Dispatch(cmdBuf, cfg,
		min(compute_block_count, 32768u),
		compute_block_count / 32768 + 1);

	cmdBuf.end();
	queue->SubmitAndBlock(cmdBuf);
}
