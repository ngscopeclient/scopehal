/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2025 Andrew D. Zonenberg and contributors                                                         *
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
#include "DutyCycleMeasurement.h"
#include "KahanSummation.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DutyCycleMeasurement::DutyCycleMeasurement(const string& color)
	: Filter(color, CAT_MEASUREMENT)
{
	AddStream(Unit(Unit::UNIT_PERCENT), "trend", Stream::STREAM_TYPE_ANALOG);
	AddStream(Unit(Unit::UNIT_PERCENT), "avg", Stream::STREAM_TYPE_ANALOG_SCALAR);

	//Set up channels
	CreateInput("din");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DutyCycleMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;
	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string DutyCycleMeasurement::GetProtocolName()
{
	return "Duty Cycle";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DutyCycleMeasurement::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(nullptr, 0);
		return;
	}
	auto din = GetInputWaveform(0);
	auto sdin = dynamic_cast<SparseAnalogWaveform*>(din);
	auto udin = dynamic_cast<UniformAnalogWaveform*>(din);
	auto sddin = dynamic_cast<SparseDigitalWaveform*>(din);
	auto uddin = dynamic_cast<UniformDigitalWaveform*>(din);
	din->PrepareForCpuAccess();
	if(din->size() < 2)
	{
		SetData(nullptr, 0);
		return;
	}

	//Get timestamps of the edges
	//TODO: gpu accelerate
	vector<int64_t> edges;

	bool initial_polarity;
	if(sdin || udin)
	{
		//Find average voltage of the waveform and use that as the zero crossing
		float midpoint = GetAvgVoltage(sdin, udin);
		if(sdin)
			FindZeroCrossings(sdin, midpoint, edges);
		else
			FindZeroCrossings(udin, midpoint, edges);

		//Figure out edge polarity
		initial_polarity = (GetValue(sdin, udin, 0) > midpoint);
	}
	else
	{
		if(sddin)
		{
			FindZeroCrossings(sddin, edges);
			initial_polarity = sddin->m_samples[0];
		}
		else if(uddin)
		{
			FindZeroCrossings(uddin, edges);
			initial_polarity = uddin->m_samples[0];
		}
	}

	if(edges.size() < 2)
	{
		SetData(nullptr, 0);
		return;
	}

	//Create the output
	auto cap = SetupEmptySparseAnalogOutputWaveform(din, 0, true);
	cap->m_timescale = 1;
	cap->PrepareForCpuAccess();

	//Find the duty cycle per cycle, then average
	size_t elen = edges.size();
	KahanSummation sum;
	int64_t nedges = 0;
	for(size_t i=0; i < (elen - 2); i+= 2)
	{
		//measure from edge to 2 edges later, since we find all zero crossings regardless of polarity
		int64_t start = edges[i];
		int64_t mid = edges[i+1];
		int64_t end = edges[i+2];

		float t1 = mid-start;
		float t2 = end-mid;
		float total = t1+t2;

		float duty;

		//T1 is high time
		if(!initial_polarity)
			duty = t1/total;
		else
			duty = t2/total;

		cap->m_offsets.push_back(start);
		cap->m_durations.push_back(total);
		cap->m_samples.push_back(duty);

		sum += duty;
		nedges ++;
	}

	SetData(cap, 0);

	cap->MarkModifiedFromCpu();

	m_streams[1].m_value = sum.GetSum() / nedges;
}
