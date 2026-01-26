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
#include "EmphasisFilter.h"
#include "TappedDelayLineFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

EmphasisFilter::EmphasisFilter(const string& color)
	: Filter(color, CAT_ANALYSIS)
	, m_dataRate(m_parameters["Data Rate"])
	, m_emphasisType(m_parameters["Emphasis Type"])
	, m_emphasisAmount(m_parameters["Emphasis Amount"])
	, m_computePipeline("shaders/EmphasisFilter.spv", 2, sizeof(EmphasisFilterConstants))
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput("in");

	m_dataRate = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_BITRATE));
	m_dataRate.SetIntVal(1250e6);

	m_emphasisType = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_emphasisType.AddEnumValue("De-emphasis", DE_EMPHASIS);
	m_emphasisType.AddEnumValue("Pre-emphasis", PRE_EMPHASIS);
	m_emphasisType.SetIntVal(DE_EMPHASIS);

	m_emphasisAmount = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_DB));
	m_emphasisAmount.SetFloatVal(6);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool EmphasisFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string EmphasisFilter::GetProtocolName()
{
	return "Emphasis";
}

Filter::DataLocation EmphasisFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void EmphasisFilter::Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("EmphasisFilter::Refresh");
	#endif

	ClearErrors();
	if(!VerifyAllInputsOKAndUniformAnalog())
	{
		if(!GetInput(0))
			AddErrorMessage("Missing inputs", "No signal input connected");
		else if(!GetInputWaveform(0))
			AddErrorMessage("Missing inputs", "No waveform available at input");

		SetData(nullptr, 0);
		return;
	}

	//Get the input data
	auto din = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(0));
	size_t len = din->size();
	const int64_t tap_count = 2;
	if(len < tap_count)
	{
		AddErrorMessage("Input too short", "The input signal must be at least two samples long");

		SetData(nullptr, 0);
		return;
	}
	m_xAxisUnit = m_inputs[0].m_channel->GetXAxisUnits();
	SetYAxisUnits(m_inputs[0].GetYAxisUnits(), 0);

	//Set up output
	int64_t tap_delay = round(FS_PER_SECOND / m_dataRate.GetFloatVal());
	int64_t samples_per_tap = tap_delay / din->m_timescale;
	auto cap = SetupEmptyUniformAnalogOutputWaveform(din, 0, true);
	int64_t outlen = len - (tap_count * samples_per_tap);
	cap->Resize(outlen);

	//Calculate the tap values
	//Reference: "Dealing with De-Emphasis in Jitter Testing", P. Pupalaikis, LeCroy technical brief, 2008
	float db = m_emphasisAmount.GetFloatVal();
	float emphasisLevel = pow(10, -db/20);
	float coeff = 0.5 * emphasisLevel;
	float c = coeff + 0.5;
	float p = coeff - 0.5;
	float taps[tap_count] = {0};
	taps[0] = c;
	taps[1] = p;

	//If we're doing pre-emphasis rather than de-emphasis, we need to scale everything accordingly.
	auto type = static_cast<EmphasisType>(m_emphasisType.GetIntVal());
	if(type == PRE_EMPHASIS)
	{
		for(int64_t i=0; i<tap_count; i++)
			taps[i] /= emphasisLevel;
	}

	EmphasisFilterConstants cfg;
	cfg.samples_per_tap = samples_per_tap;
	cfg.size = outlen;
	cfg.tap0 = taps[0];
	cfg.tap1 = taps[1];

	cmdBuf.begin({});

	m_computePipeline.BindBufferNonblocking(0, din->m_samples, cmdBuf);
	m_computePipeline.BindBufferNonblocking(1, cap->m_samples, cmdBuf, true);

	const uint32_t compute_block_count = GetComputeBlockCount(outlen, 64);
	m_computePipeline.Dispatch(cmdBuf, cfg,
		min(compute_block_count, 32768u),
		compute_block_count / 32768 + 1);

	cmdBuf.end();
	queue->SubmitAndBlock(cmdBuf);

	cap->m_samples.MarkModifiedFromGpu();
}
