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
#include "ExponentialMovingAverageFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ExponentialMovingAverageFilter::ExponentialMovingAverageFilter(const string& color)
	: Filter(color, CAT_MATH)
	, m_halflife(m_parameters["Half-life"])
	, m_computePipeline("shaders/ExponentialMovingAverage.spv", 2, sizeof(ExponentialMovingAverageConstants))
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput("din");

	m_halflife = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_halflife.SetIntVal(8);
}

ExponentialMovingAverageFilter::~ExponentialMovingAverageFilter()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool ExponentialMovingAverageFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if(i >= 1)
		return false;

	if(stream.GetType() == Stream::STREAM_TYPE_ANALOG)
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string ExponentialMovingAverageFilter::GetProtocolName()
{
	return "Exponential Moving Average";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void ExponentialMovingAverageFilter::ClearSweeps()
{
	SetData(nullptr, 0);
}

void ExponentialMovingAverageFilter::Refresh(
	vk::raii::CommandBuffer& cmdBuf,
	shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("ExponentialMovingAverageFilter::Refresh");
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

	//Get inputs
	auto din = GetInputWaveform(0);
	auto sdin = dynamic_cast<SparseAnalogWaveform*>(din);
	auto udin = dynamic_cast<UniformAnalogWaveform*>(din);
	size_t len = din->size();

	//Set up units
	m_xAxisUnit = m_inputs[0].m_channel->GetXAxisUnits();
	SetYAxisUnits(m_inputs[0].GetYAxisUnits(), 0);

	//See if we already had valid output data
	auto cap = GetData(0);
	auto scap = dynamic_cast<SparseAnalogWaveform*>(cap);
	auto ucap = dynamic_cast<UniformAnalogWaveform*>(cap);

	//Convert half life to decay coefficient
	float hl = m_halflife.GetIntVal();
	ExponentialMovingAverageConstants cfg;
	cfg.size = len;
	cfg.decay = 1 / pow(2, 1/hl);

	cmdBuf.begin({});

	bool skip = false;

	//Sparse path
	if(sdin)
	{
		//No data? Just copy
		if(!scap)
		{
			scap = new SparseAnalogWaveform;
			scap->Resize(din->size());
			cap = scap;
			SetData(cap, 0);

			scap->m_samples.CopyFrom(sdin->m_samples);
			skip = true;
		}

		//Actual filter code path
		else
		{
			scap->Resize(din->size());
			m_computePipeline.BindBufferNonblocking(0, scap->m_samples, cmdBuf);
			m_computePipeline.BindBufferNonblocking(1, sdin->m_samples, cmdBuf);
		}

		//Either way we want to reuse the timestamps
		scap->CopyTimestamps(sdin);
	}

	//Uniform path
	else
	{
		//No data? Just copy
		if(!ucap)
		{
			ucap = new UniformAnalogWaveform;
			ucap->Resize(din->size());
			cap = ucap;
			SetData(cap, 0);

			ucap->m_samples.CopyFrom(udin->m_samples);
			skip = true;
		}

		//Actual filter code path
		else
		{
			ucap->Resize(din->size());
			m_computePipeline.BindBufferNonblocking(0, ucap->m_samples, cmdBuf);
			m_computePipeline.BindBufferNonblocking(1, udin->m_samples, cmdBuf);
		}
	}

	//Run the actual decay shader
	if(!skip)
	{
		const uint32_t compute_block_count = GetComputeBlockCount(len, 64);
		m_computePipeline.Dispatch(cmdBuf, cfg,
			min(compute_block_count, 32768u),
			compute_block_count / 32768 + 1);
	}

	cmdBuf.end();
	queue->SubmitAndBlock(cmdBuf);
	cap->MarkModifiedFromGpu();

	//Update timestamps
	cap->m_startTimestamp 		= din->m_startTimestamp;
	cap->m_startFemtoseconds	= din->m_startFemtoseconds;
	cap->m_triggerPhase			= din->m_triggerPhase;
	cap->m_timescale			= din->m_timescale;
	cap->m_revision ++;
}

Filter::DataLocation ExponentialMovingAverageFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}
