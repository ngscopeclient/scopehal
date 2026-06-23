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
#include "TwoPortShuntThroughFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

TwoPortShuntThroughFilter::TwoPortShuntThroughFilter(const string& color)
	: Filter(color, CAT_RF)
{
	AddStream(Unit(Unit::UNIT_OHMS), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput<InputConstraintAND>(
		"S21Mag",
		initializer_list<shared_ptr<InputConstraint> >
		{
			make_shared<InputConstraintStreamType>(this, Stream::STREAM_TYPE_ANALOG),
			make_shared<InputConstraintXUnit>(this, Unit(Unit::UNIT_HZ)),
			make_shared<InputConstraintYUnit>(this, Unit(Unit::UNIT_DB))
		});

	m_xAxisUnit = Unit(Unit::UNIT_HZ);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string TwoPortShuntThroughFilter::GetProtocolName()
{
	return "2-Port Shunt Through";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void TwoPortShuntThroughFilter::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue
	)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("TwoPortShuntThroughFilter::Refresh");
	#endif
	ClearErrors();

	if(!VerifyAllInputsOK())
	{
		AddErrorMessage("Missing input", "One or more inputs are unconnected");
		SetData(nullptr, 0);
		return;
	}

	auto din = GetInputWaveform(0);
	auto umag = dynamic_cast<UniformAnalogWaveform*>(din);
	auto smag = dynamic_cast<SparseAnalogWaveform*>(din);
	din->PrepareForCpuAccess();

	//We need meaningful data
	size_t len = din->size();
	if(len == 0)
	{
		AddErrorMessage("Empty input", "Need at least one input sample");
		SetData(nullptr, 0);
		return;
	}
	else
		len --;

	//Create the output and copy timestamps
	auto cap = SetupEmptySparseAnalogOutputWaveform(din, 0, true);
	cap->PrepareForCpuAccess();
	cap->Resize(len);
	cap->m_timescale = 1;

	//Main output loop
	for(size_t i=0; i<len; i++)
	{
		auto s21_log = GetValue(smag, umag, i);
		float s21_mag = pow(10, s21_log / 20);

		//TODO: make this a parameter for VNA config?
		const float z0 = 50;

		cap->m_offsets[i] = GetOffsetScaled(smag, umag, i);
		cap->m_durations[i] = GetDurationScaled(smag, umag, i);
		cap->m_samples[i] = ((0.5 * z0) * s21_mag) / (1 - s21_mag);
	}

	cap->MarkModifiedFromCpu();
}
