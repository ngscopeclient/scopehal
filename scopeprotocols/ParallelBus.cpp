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
#include "ParallelBus.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ParallelBus::ParallelBus(const string& color)
	: Filter(color, CAT_BUS)
	, m_width(m_parameters["Width"])
{
	AddStream( Unit(Unit::UNIT_COUNTS), "data", Stream::STREAM_TYPE_DIGITAL_BUS);

	m_width = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_width.SetIntVal(8);
	m_width.signal_changed().connect(sigc::mem_fun(*this, &ParallelBus::OnWidthChanged));

	OnWidthChanged();
}

void ParallelBus::OnWidthChanged()
{
	//Create new ports
	size_t nports = m_width.GetIntVal();

	//Set up channels
	char tmp[32];
	for(size_t i=0; i<nports; i++)
	{
		//If we already have this input, do nothing
		if(i < m_inputs.size())
			continue;

		snprintf(tmp, sizeof(tmp), "din%zu", i);
		CreateInput<InputConstraintStreamType>(tmp, Stream::STREAM_TYPE_DIGITAL);
	}

	//Delete extra inputs
	for(size_t i=nports; i<m_inputs.size(); i++)
		SetInput(i, nullptr, true);
	m_inputs.resize(nports);

	m_inputsChangedSignal.emit();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string ParallelBus::GetProtocolName()
{
	return "Parallel Bus";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void ParallelBus::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("ParallelBus::Refresh");
	#endif
	ClearMessages();

	//Figure out how wide our input is
	//TODO: 64 bit support
	size_t width = m_width.GetIntVal();
	if( (width > 32) || (width <= 0) )
	{
		AddErrorMessage("Invalid configuration", "Input must be 1-32 bits");
		SetData(nullptr, 0);
		return;
	}

	//Make sure we have an input for each channel in use
	vector<UniformDigitalWaveform*> inputs;
	for(size_t i=0; i<width; i++)
	{
		auto din = dynamic_cast<UniformDigitalWaveform*>(GetInputWaveform(i));
		if(din == nullptr)
		{
			AddErrorMessage("Missing input", "One or more inputs are unconnected or invalid");
			SetData(nullptr, 0);
			return;
		}
		din->PrepareForCpuAccess();
		inputs.push_back(din);
	}
	if(inputs.empty())
	{
		AddErrorMessage("Missing input", "No inputs provided");
		SetData(nullptr, 0);
		return;
	}

	//Figure out length of the output
	size_t len = inputs[0]->m_samples.size();
	for(size_t j=1; j<width; j++)
		len = min(len, inputs[j]->m_samples.size());

	//Make the output waveform
	auto cap = SetupEmptyWaveform<UniformDigitalBusWaveform32>(inputs[0], 0);
	cap->PrepareForCpuAccess();
	cap->Resize(len);

	//Pack it
	//TODO: how to shader with dynamic number of inputs? do we make one for each count and dynamic dispatch?
	for(size_t i=0; i<len; i++)
	{
		uint32_t n = 0;
		for(size_t j=0; j<width; j++)
			n |= inputs[j]->m_samples[i] << j;
		cap->m_samples[i] = n;
	}

	cap->MarkModifiedFromCpu();
}
