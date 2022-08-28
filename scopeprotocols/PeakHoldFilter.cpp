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
#include "PeakHoldFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PeakHoldFilter::PeakHoldFilter(const string& color)
	: PeakDetectionFilter(color, CAT_MATH)
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput("din");

	//Copy input unit
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool PeakHoldFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string PeakHoldFilter::GetProtocolName()
{
	return "Peak Hold";
}

void PeakHoldFilter::ClearSweeps()
{
	SetData(NULL, 0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void PeakHoldFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Copy units
	m_xAxisUnit = m_inputs[0].m_channel->GetXAxisUnits();
	SetYAxisUnits(m_inputs[0].GetYAxisUnits(), 0);

	auto din = GetInputWaveform(0);

	//Create waveform if we don't have one already
	size_t len = din->size();
	bool first = false;

	auto sdin = dynamic_cast<SparseAnalogWaveform*>(din);
	auto udin = dynamic_cast<UniformAnalogWaveform*>(din);

	if(sdin)
	{
		auto cap = dynamic_cast<SparseAnalogWaveform*>(GetData(0));
		if(cap == NULL)
		{
			cap = new SparseAnalogWaveform;
			cap->Resize(len);
			SetData(cap, 0);
			first = true;
		}

		//If sample size changed, clear it out
		if(cap->size() != len)
		{
			cap->Resize(len);
			first = true;
		}

		//Copy our time scales from the input
		cap->m_timescale = din->m_timescale;
		cap->m_startTimestamp = din->m_startTimestamp;
		cap->m_startFemtoseconds = din->m_startFemtoseconds;

		//Copy timestamps from the input
		cap->CopyTimestamps(sdin);

		//First waveform just copies the input
		if(first)
		{
			for(size_t i=0; i<len; i++)
				cap->m_samples[i] = sdin->m_samples[i];
		}

		//otherwise actually do peak holding
		else
		{
			for(size_t i=0; i<len; i++)
				cap->m_samples[i] = max((float)cap->m_samples[i], (float)sdin->m_samples[i]);
		}

		FindPeaks(cap);
	}
	else
	{
		auto cap = dynamic_cast<UniformAnalogWaveform*>(GetData(0));
		if(cap == NULL)
		{
			cap = new UniformAnalogWaveform;
			cap->Resize(len);
			SetData(cap, 0);
			first = true;
		}

		//If sample size changed, clear it out
		if(cap->size() != len)
		{
			cap->Resize(len);
			first = true;
		}

		//Copy our time scales from the input
		cap->m_timescale = din->m_timescale;
		cap->m_startTimestamp = din->m_startTimestamp;
		cap->m_startFemtoseconds = din->m_startFemtoseconds;

		//First waveform just copies the input
		if(first)
		{
			for(size_t i=0; i<len; i++)
				cap->m_samples[i] = udin->m_samples[i];
		}

		//otherwise actually do peak holding
		else
		{
			for(size_t i=0; i<len; i++)
				cap->m_samples[i] = max((float)cap->m_samples[i], (float)udin->m_samples[i]);
		}

		FindPeaks(cap);
	}
}
