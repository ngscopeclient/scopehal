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
#include "PhaseNonlinearityFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PhaseNonlinearityFilter::PhaseNonlinearityFilter(const string& color)
	: Filter(color, CAT_RF)
	, m_refLowName("Ref Freq Low")
	, m_refHighName("Ref Freq High")
{
	AddStream(Unit(Unit::UNIT_DEGREES), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput("Phase");

	m_parameters[m_refLowName] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_HZ));
	m_parameters[m_refLowName].SetIntVal(1e9);

	m_parameters[m_refHighName] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_HZ));
	m_parameters[m_refHighName].SetIntVal(2e9);

	m_xAxisUnit = Unit(Unit::UNIT_HZ);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool PhaseNonlinearityFilter::ValidateChannel(size_t i, StreamDescriptor stream)
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

string PhaseNonlinearityFilter::GetProtocolName()
{
	return "Phase Nonlinearity";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void PhaseNonlinearityFilter::Refresh()
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

	//Calculate the average group delay (ΔPhase / ΔFreq) between our reference frequencies
	//We need to do a linear search here because we have to unwrap phases as we go.
	//Note that the value calculated here is in units of degrees per Hz, not cycles per Hz (seconds).
	//We don't need to convert to time units since we're about to integrate it wrt dFreq to compute the nominal phase.
	float initialPhase = GetValue(sang, uang, 0);
	float phase = initialPhase;
	float phaseLow = 0;
	float phaseHigh = 0;
	int64_t freqLow = m_parameters[m_refLowName].GetIntVal();
	int64_t freqHigh = m_parameters[m_refHighName].GetIntVal();
	bool foundFreqLow = false;
	for(size_t i=0; i<len; i++)
	{
		//Compute unwrapped phase
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

		int64_t freq = GetOffsetScaled(sang, uang, i);

		//Find first point above lower ref freq
		if(!foundFreqLow && (freq > freqLow) )
		{
			foundFreqLow = true;
			freqLow = freq;
			phaseLow = phase;
		}

		//Find first point above upper ref freq)
		if(freq > freqHigh)
		{
			phaseHigh = phase;
			freqHigh = freq;
			break;
		}
	}
	float groupDelay = (phaseHigh - phaseLow) / (freqHigh - freqLow);

	//Main output loop
	int64_t initialFreq = GetOffsetScaled(sang, uang, 0);
	phase = initialPhase;
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

		//Calculate nominal phase for a linear network
		int64_t freq = GetOffsetScaled(sang, uang, i);
		float nominalPhase = groupDelay * (freq - initialFreq) + initialPhase;

		cap->m_offsets[i] = freq;
		cap->m_durations[i] = GetDurationScaled(sang, uang, i);
		cap->m_samples[i] = phase - nominalPhase;
	}

	cap->MarkModifiedFromCpu();
}
