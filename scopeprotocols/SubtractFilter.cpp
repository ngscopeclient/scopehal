/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2024 Andrew D. Zonenberg and contributors                                                         *
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
#include "SubtractFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SubtractFilter::SubtractFilter(const string& color)
	: Filter(color, CAT_MATH)
	, m_computePipeline("shaders/SubtractFilter.spv", 3, sizeof(SubtractFilterConstants))
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput("IN+");
	CreateInput("IN-");
}

SubtractFilter::~SubtractFilter()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool SubtractFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if(i >= 2)
		return false;

	if( (stream.GetType() == Stream::STREAM_TYPE_ANALOG) || (stream.GetType() == Stream::STREAM_TYPE_ANALOG_SCALAR) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string SubtractFilter::GetProtocolName()
{
	return "Subtract";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void SubtractFilter::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	//Set units as early as possible so we can spawn in the same plot as our parent signal when creating a filter
	if(GetInput(0))
	{
		m_xAxisUnit = m_inputs[0].m_channel->GetXAxisUnits();
		SetYAxisUnits(m_inputs[0].GetYAxisUnits(), 0);
	}

	bool veca = GetInput(0).GetType() == Stream::STREAM_TYPE_ANALOG;
	bool vecb = GetInput(1).GetType() == Stream::STREAM_TYPE_ANALOG;

	if(veca && vecb)
		DoRefreshVectorVector(cmdBuf, queue);
	else if(!veca && !vecb)
		DoRefreshScalarScalar();
	else if(veca)
		DoRefreshScalarVector(1, 0);
	else
		DoRefreshScalarVector(0, 1);
}

void SubtractFilter::DoRefreshScalarScalar()
{
	m_streams[0].m_stype = Stream::STREAM_TYPE_ANALOG_SCALAR;
	SetData(nullptr, 0);

	//Subtract value
	//TODO: how to handle unequal units?
	m_streams[0].m_yAxisUnit = GetInput(0).GetYAxisUnits();
	m_streams[0].m_value = GetInput(0).GetScalarValue() - GetInput(1).GetScalarValue();
}

void SubtractFilter::DoRefreshVectorVector(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue)
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get inputs
	auto din_p = GetInputWaveform(0);
	auto din_n = GetInputWaveform(1);
	auto sdin_p = dynamic_cast<SparseAnalogWaveform*>(din_p);
	auto sdin_n = dynamic_cast<SparseAnalogWaveform*>(din_n);
	auto udin_p = dynamic_cast<UniformAnalogWaveform*>(din_p);
	auto udin_n = dynamic_cast<UniformAnalogWaveform*>(din_n);

	//Set up units and complain if they're inconsistent
	if( (m_xAxisUnit != m_inputs[1].m_channel->GetXAxisUnits()) ||
		(m_inputs[0].GetYAxisUnits() != m_inputs[1].GetYAxisUnits()) )
	{
		SetData(NULL, 0);
		return;
	}

	//Waveforms must be equal sample *rate* to make things work as expected.
	//But if they don't have the same trigger phase, we can easily correct for that..
	Unit fs(Unit::UNIT_FS);
	int64_t skew = llabs(din_p->m_triggerPhase - din_n->m_triggerPhase);

	//Convert calculated skew to offset in samples from start of each waveform
	size_t offsetP = 0;
	size_t offsetN = 0;
	if(din_p->m_triggerPhase > din_n->m_triggerPhase)
		offsetN = skew / din_n->m_timescale;
	else
		offsetP = skew / din_p->m_timescale;

	//Bail if the waveforms don't overlap
	if(offsetP > din_p->size())
		return;
	if(offsetN > din_n->size())
		return;

	//We need meaningful data after any offset that may have been applied
	size_t len = min( (din_p->size() - offsetP), (din_n->size() - offsetN) );

	//Setup output waveform
	UniformAnalogWaveform* ucap = nullptr;
	SparseAnalogWaveform* scap = nullptr;
	if(sdin_p && sdin_n)
	{
		scap = SetupSparseOutputWaveform(sdin_p, 0, 0, 0);
		scap->m_triggerPhase = max(din_p->m_triggerPhase, din_n->m_triggerPhase);
	}
	else if(udin_p && udin_n)
	{
		ucap = SetupEmptyUniformAnalogOutputWaveform(udin_p, 0);
		ucap->m_triggerPhase = max(din_p->m_triggerPhase, din_n->m_triggerPhase);
		ucap->Resize(len);
	}

	//Mixed sparse/uniform not allowed
	else
	{
		SetData(NULL, 0);
		return;
	}

	//Special case if input units are degrees: we want to do modular arithmetic
	//TODO: vectorized version of this
	if(GetYAxisUnits(0) == Unit::UNIT_DEGREES)
	{
		//Waveform data must be on CPU
		din_p->PrepareForCpuAccess();
		din_n->PrepareForCpuAccess();
		if(scap)
			scap->PrepareForCpuAccess();
		else
			ucap->PrepareForCpuAccess();

		float* out = scap ? scap->m_samples.GetCpuPointer() : ucap->m_samples.GetCpuPointer();
		float* a = sdin_p ? sdin_p->m_samples.GetCpuPointer() : udin_p->m_samples.GetCpuPointer();
		float* b = sdin_n ? sdin_n->m_samples.GetCpuPointer() : udin_n->m_samples.GetCpuPointer();

		for(size_t i=0; i<len; i++)
		{
			out[i] 		= a[i + offsetP] - b[i + offsetN];
			if(out[i] < -180)
				out[i] += 360;
			if(out[i] > 180)
				out[i] -= 360;
		}

		if(scap)
			scap->m_samples.MarkModifiedFromCpu();
		else
			ucap->m_samples.MarkModifiedFromCpu();
	}

	//Just regular subtraction, use the GPU filter
	else
	{
		cmdBuf.begin({});

		SubtractFilterConstants cfg;
		cfg.offsetP = offsetP;
		cfg.offsetN = offsetN;
		cfg.size = len;

		m_computePipeline.BindBufferNonblocking(0, sdin_p ? sdin_p->m_samples : udin_p->m_samples, cmdBuf);
		m_computePipeline.BindBufferNonblocking(1, sdin_n ? sdin_n->m_samples : udin_n->m_samples, cmdBuf);
		m_computePipeline.BindBufferNonblocking(2, scap ? scap->m_samples : ucap->m_samples, cmdBuf, true);
		const uint32_t compute_block_count = GetComputeBlockCount(len, 64);
		m_computePipeline.Dispatch(cmdBuf, cfg,
			min(compute_block_count, 32768u),
			compute_block_count / 32768 + 1);

		cmdBuf.end();
		queue->SubmitAndBlock(cmdBuf);

		if(scap)
			scap->m_samples.MarkModifiedFromGpu();
		else
			ucap->m_samples.MarkModifiedFromGpu();
	}
}

void SubtractFilter::DoRefreshScalarVector(size_t iScalar, size_t iVector)
{
	m_streams[0].m_stype = Stream::STREAM_TYPE_ANALOG;

	float scale = GetInput(iScalar).GetScalarValue();
	auto din = GetInputWaveform(iVector);
	if(!din)
	{
		SetData(nullptr, 0);
		return;
	}
	din->PrepareForCpuAccess();
	auto len = din->size();

	auto sparse = dynamic_cast<SparseAnalogWaveform*>(din);
	auto uniform = dynamic_cast<UniformAnalogWaveform*>(din);

	if(sparse)
	{
		//Set up the output waveform
		auto cap = SetupSparseOutputWaveform(sparse, 0, 0, 0);
		cap->Resize(len);
		cap->PrepareForCpuAccess();

		//subtract is slightly more complex than adding because we have to keep the order right
		float* fin = (float*)__builtin_assume_aligned(sparse->m_samples.GetCpuPointer(), 16);
		float* fdst = (float*)__builtin_assume_aligned(cap->m_samples.GetCpuPointer(), 16);
		if(iScalar == 1)
		{
			for(size_t i=0; i<len; i++)
				fdst[i] = fin[i] - scale;
		}
		else
		{
			for(size_t i=0; i<len; i++)
				fdst[i] = scale - fin[i];
		}

		cap->MarkModifiedFromCpu();
	}
	else
	{
		//Set up the output waveform
		auto cap = SetupEmptyUniformAnalogOutputWaveform(uniform, 0);
		cap->Resize(len);
		cap->PrepareForCpuAccess();

		float* fin = (float*)__builtin_assume_aligned(uniform->m_samples.GetCpuPointer(), 16);
		float* fdst = (float*)__builtin_assume_aligned(cap->m_samples.GetCpuPointer(), 16);
		if(iScalar == 1)
		{
			for(size_t i=0; i<len; i++)
				fdst[i] = fin[i] - scale;
		}
		else
		{
			for(size_t i=0; i<len; i++)
				fdst[i] = scale - fin[i];
		}

		cap->MarkModifiedFromCpu();
	}
}

Filter::DataLocation SubtractFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}
