/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2022 Andrew D. Zonenberg and contributors                                                         *
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
#include "UnwrappedPhaseFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

UnwrappedPhaseFilter::UnwrappedPhaseFilter(const string& color)
	: Filter(color, CAT_RF)
{
	AddStream(Unit(Unit::UNIT_DEGREES), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput("Phase");

	m_xAxisUnit = Unit(Unit::UNIT_HZ);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool UnwrappedPhaseFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;
	if(stream.GetType() != Stream::STREAM_TYPE_ANALOG)
		return false;
	if(stream.m_channel->GetXAxisUnits().GetType() != Unit::UNIT_HZ)
		return false;
	if(i == 0)
		return (stream.GetYAxisUnits().GetType() == Unit::UNIT_DEGREES);

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string UnwrappedPhaseFilter::GetProtocolName()
{
	return "Unwrapped Phase";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void UnwrappedPhaseFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	auto din = GetInputWaveform(0);
	auto uang = dynamic_cast<UniformAnalogWaveform*>(din);
	auto sang = dynamic_cast<SparseAnalogWaveform*>(din);
	din->PrepareForCpuAccess();

	//We need meaningful data
	size_t len = din->size();
	if(len == 0)
	{
		SetData(NULL, 0);
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
	float phase = GetValue(sang, uang, 0);
	for(size_t i=0; i<len; i++)
	{
		//Subtract phase angles, wrapping correctly around singularities
		//(assume +/- 180 deg range at input)
		float phase_hi = GetValue(sang, uang, i+1);
		float phase_lo = GetValue(sang, uang, i);
		if(fabs(phase_lo - phase_hi) > 180)
		{
			if(phase_lo < phase_hi)
				phase_lo += 360;
			else
				phase_hi += 360;
		}
		float dphase = phase_hi - phase_lo;

		phase += dphase;

		cap->m_offsets[i] = GetOffsetScaled(sang, uang, i);
		cap->m_durations[i] = GetDurationScaled(sang, uang, i);
		cap->m_samples[i] = phase;
	}

	cap->MarkModifiedFromCpu();
}
