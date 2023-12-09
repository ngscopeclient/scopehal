/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2023 Andrew D. Zonenberg and contributors                                                         *
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
	, m_halflife("Half-life")
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput("din");

	m_parameters[m_halflife] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_halflife].SetIntVal(8);
}

ExponentialMovingAverageFilter::~ExponentialMovingAverageFilter()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool ExponentialMovingAverageFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
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

void ExponentialMovingAverageFilter::Refresh(vk::raii::CommandBuffer& /*cmdBuf*/, shared_ptr<QueueHandle> /*queue*/)
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(nullptr, 0);
		return;
	}

	//Get inputs
	auto din = GetInputWaveform(0);
	auto sdin = dynamic_cast<SparseAnalogWaveform*>(din);
	auto udin = dynamic_cast<UniformAnalogWaveform*>(din);
	size_t len = din->size();

	//Convert half life to decay coefficient
	float hl = m_parameters[m_halflife].GetIntVal();
	float decay = 1 / pow(2, 1/hl);

	din->PrepareForCpuAccess();

	//Set up units
	m_xAxisUnit = m_inputs[0].m_channel->GetXAxisUnits();
	SetYAxisUnits(m_inputs[0].GetYAxisUnits(), 0);

	//See if we already had valid output data
	auto cap = GetData(0);
	auto scap = dynamic_cast<SparseAnalogWaveform*>(cap);
	auto ucap = dynamic_cast<UniformAnalogWaveform*>(cap);
	if(cap)
		cap->PrepareForCpuAccess();

	//Sparse path
	if(sdin)
	{
		//No data? Just copy
		if(!scap)
		{
			scap = new SparseAnalogWaveform;
			scap->Resize(din->size());
			cap = scap;

			scap->m_samples.CopyFrom(sdin->m_samples);
		}

		//Actual filter code path
		else
		{
			auto pin = sdin->m_samples.GetCpuPointer();
			auto pout = scap->m_samples.GetCpuPointer();

			for(size_t i=0; i<len; i++)
				pout[i] = pout[i]*decay + pin[i]*(1-decay);
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

			ucap->m_samples.CopyFrom(udin->m_samples);
		}

		//Actual filter code path
		else
		{
			auto pin = udin->m_samples.GetCpuPointer();
			auto pout = ucap->m_samples.GetCpuPointer();

			for(size_t i=0; i<len; i++)
				pout[i] = pout[i]*decay + pin[i]*(1-decay);
		}
	}

	//Update timestamps
	cap->m_startTimestamp 		= din->m_startTimestamp;
	cap->m_startFemtoseconds	= din->m_startFemtoseconds;
	cap->m_triggerPhase			= din->m_triggerPhase;
	cap->m_timescale			= din->m_timescale;
	cap->m_revision ++;

	//Done
	SetData(cap, 0);
	cap->MarkModifiedFromCpu();
}

Filter::DataLocation ExponentialMovingAverageFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}
