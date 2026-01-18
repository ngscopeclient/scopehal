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
#include "ClipFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ClipFilter::ClipFilter(const string& color)
	: Filter(color, CAT_MATH)
	, m_clipAbove(m_parameters["Behavior"])
	, m_clipLevel(m_parameters["Level"])
	, m_computePipeline("shaders/ClipFilter.spv", 2, sizeof(ClipFilterConstants))
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput("din");

	m_clipAbove = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_clipAbove.AddEnumValue("Clip Above", 1);
	m_clipAbove.AddEnumValue("Clip Below", 0);
	m_clipAbove.SetIntVal(0);

	m_clipLevel = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_clipLevel.SetFloatVal(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool ClipFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string ClipFilter::GetProtocolName()
{
	return "Clip";
}

Filter::DataLocation ClipFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void ClipFilter::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
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

	auto din = GetInputWaveform(0);
	auto udin = dynamic_cast<UniformAnalogWaveform*>(din);
	auto sdin = dynamic_cast<SparseAnalogWaveform*>(din);
	if(!sdin && !udin)
	{
		AddErrorMessage("Missing inputs", "No waveform available at input");
		SetData(nullptr, 0);
		return;
	}

	cmdBuf.begin({});

	//Push constants
	size_t len = din->size();
	ClipFilterConstants cfg;
	cfg.len = len;
	cfg.clipAbove = m_clipAbove.GetIntVal();
	cfg.level = m_clipLevel.GetFloatVal();

	//Set up output waveform and bind buffers
	if(sdin)
	{
		auto cap = SetupSparseOutputWaveform(sdin, 0, 0, 0);
		m_computePipeline.BindBufferNonblocking(0, sdin->m_samples, cmdBuf);
		m_computePipeline.BindBufferNonblocking(1, cap->m_samples, cmdBuf, true);
		cap->MarkSamplesModifiedFromGpu();
	}
	else if(udin)
	{
		auto cap = SetupEmptyUniformAnalogOutputWaveform(udin, 0);
		cap->Resize(len);
		m_computePipeline.BindBufferNonblocking(0, udin->m_samples, cmdBuf);
		m_computePipeline.BindBufferNonblocking(1, cap->m_samples, cmdBuf, true);
		cap->MarkSamplesModifiedFromGpu();
	}

	//Do the actual clipping
	const uint32_t compute_block_count = GetComputeBlockCount(len, 64);
	m_computePipeline.Dispatch(cmdBuf, cfg,
		min(compute_block_count, 32768u),
		compute_block_count / 32768 + 1);

	cmdBuf.end();
	queue->SubmitAndBlock(cmdBuf);
}
