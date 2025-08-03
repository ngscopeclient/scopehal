/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2025 Andrew D. Zonenberg and contributors                                                         *
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
#include "Waterfall.h"
#include "FFTFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

WaterfallWaveform::WaterfallWaveform(size_t width, size_t height)
	: DensityFunctionWaveform(width, height)
	, m_tempBuf("WaterfallWaveform.m_tempBuf")
{
	//Temporary buffer is GPU-only
	m_tempBuf.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_NEVER);
	m_tempBuf.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);

	m_tempBuf.resize(width*height);
}

WaterfallWaveform::~WaterfallWaveform()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

Waterfall::Waterfall(const string& color)
	: Filter(color, CAT_RF)
	, m_width(1)
	, m_height(1)
	, m_maxwidth("Max width")
	, m_computePipeline("shaders/WaterfallFilter.spv", 3, sizeof(WaterfallFilterArgs))
{
	AddStream(Unit(Unit::UNIT_DBM), "data", Stream::STREAM_TYPE_WATERFALL);
	m_xAxisUnit = Unit(Unit::UNIT_HZ);

	m_parameters[m_maxwidth] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLEDEPTH));
	m_parameters[m_maxwidth].SetIntVal(131072);

	//Set up channels
	CreateInput("Spectrum");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool Waterfall::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) &&
		(stream.GetType() == Stream::STREAM_TYPE_ANALOG) &&
		(stream.m_channel->GetXAxisUnits() == Unit::UNIT_HZ)
		)
	{
		return true;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

float Waterfall::GetOffset(size_t /*stream*/)
{
	return 0;
}

float Waterfall::GetVoltageRange(size_t /*stream*/)
{
	return 1;
}

string Waterfall::GetProtocolName()
{
	return "Waterfall";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void Waterfall::ClearSweeps()
{
	SetData(nullptr, 0);
}

void Waterfall::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndUniformAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(0));
	size_t inlen = din->size();

	//Figure out how wide we want the input capture to be
	size_t maxwidth = m_parameters[m_maxwidth].GetIntVal();
	size_t capwidth = min(maxwidth, inlen);

	//Reallocate if input size changed, or we don't have an input capture at all
	auto cap = dynamic_cast<WaterfallWaveform*>(GetData(0));
	if( (cap == nullptr) || (m_width != capwidth) || (m_width != cap->GetWidth()) || (m_height != cap->GetHeight()) )
	{
		cap = new WaterfallWaveform(capwidth, m_height);
		m_width = capwidth;
		SetData(cap, 0);
	}

	//Figure out the frequency span of the input
	int64_t spanIn = din->m_timescale * inlen;

	//Recalculate timescale and update timestamps
	cap->m_timescale = spanIn / capwidth;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;

	//Calculate some coefficients
	WaterfallFilterArgs args;
	args.width = m_width;
	args.height = m_height;
	args.inlen = inlen;
	args.vrange = m_inputs[0].GetVoltageRange(); //db from min to max scale
	args.vfs = args.vrange/2 - m_inputs[0].GetOffset();

	//TODO: is this OK or are we going to lose too much precision doing this?
	args.timescaleRatio = cap->m_timescale * 1.0 / din->m_timescale;

	//Make sure input is ready
	din->PrepareForGpuAccess();
	cap->PrepareForGpuAccess();
	cap->m_tempBuf.PrepareForGpuAccess();

	cmdBuf.begin({});

	//Run the actual compute on the GPU
	m_computePipeline.BindBufferNonblocking(0, din->m_samples, cmdBuf);
	m_computePipeline.BindBufferNonblocking(1, cap->GetOutData(), cmdBuf);
	m_computePipeline.BindBufferNonblocking(2, cap->m_tempBuf, cmdBuf, true);
	const uint32_t compute_block_count = GetComputeBlockCount(args.width, 64);
	m_computePipeline.Dispatch(
		cmdBuf, args,
		min(compute_block_count, 32768u),
		m_height,
		compute_block_count / 32768 + 1);

	//Wait for the shader to finish
	cmdBuf.pipelineBarrier(
		vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eTransfer,
		vk::PipelineStageFlagBits::eTransfer,
		{},
		vk::MemoryBarrier(
			vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eTransferWrite,
			vk::AccessFlagBits::eTransferRead),
		{},
		{});

	//Copy the output buffer over the input
	vk::BufferCopy region(0, 0, cap->GetOutData().size() * sizeof(float));
	cmdBuf.copyBuffer(cap->m_tempBuf.GetBuffer(), cap->GetOutData().GetBuffer(), {region});

	cmdBuf.end();
	queue->SubmitAndBlock(cmdBuf);

	cap->GetOutData().MarkModifiedFromGpu();
}
