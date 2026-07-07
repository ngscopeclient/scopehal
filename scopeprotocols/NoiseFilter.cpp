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
#include "NoiseFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

NoiseFilter::NoiseFilter(const string& color)
	: Filter(color, CAT_GENERATION)
	, m_stdev(m_parameters["Deviation"])
	, m_twister(rand())
	, m_computePipeline("shaders/NoiseFilter.spv", 2, sizeof(NoiseFilterConstants))
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput<InputConstraintStreamType>("din", Stream::STREAM_TYPE_ANALOG);

	m_stdev = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_stdev.SetFloatVal(0.005);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string NoiseFilter::GetProtocolName()
{
	return "Noise";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void NoiseFilter::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	//Make sure we've got valid inputs
	//TODO: sparse path
	ClearErrors();
	if(!VerifyAllInputsOKAndUniformAnalog())
	{
		AddErrorMessage("Invalid input", "Expected uniform analog input");
		SetData(nullptr, 0);
		return;
	}

	auto din = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(0));
	din->PrepareForCpuAccess();
	size_t len = din->size();

	auto cap = SetupEmptyUniformAnalogOutputWaveform(din, 0);
	cap->Resize(len);

	cmdBuf.begin({});

	//Push constants
	NoiseFilterConstants cfg;
	cfg.size = len;
	cfg.rngSeed = m_twister();
	cfg.sigma = m_stdev.GetFloatVal();

	m_computePipeline.BindBufferNonblocking(0, din->m_samples, cmdBuf);
	m_computePipeline.BindBufferNonblocking(1, cap->m_samples, cmdBuf, true);
	const uint32_t compute_block_count = GetComputeBlockCount(len, 64 * 2);	//2 samples per thread
	m_computePipeline.Dispatch(cmdBuf, cfg,
		min(compute_block_count, 32768u),
		compute_block_count / 32768 + 1);

	cmdBuf.end();
	queue->SubmitAndBlock(cmdBuf);

	cap->MarkModifiedFromGpu();
}
