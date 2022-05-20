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

#include "scopeprotocols.h"
#include "PeriodMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PeriodMeasurement::PeriodMeasurement(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MEASUREMENT)
{
	SetYAxisUnits(Unit(Unit::UNIT_FS), 0);

	//Set up channels
	CreateInput("din");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool PeriodMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string PeriodMeasurement::GetProtocolName()
{
	return "Period";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void PeriodMeasurement::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	//Find average voltage of the waveform and use that as the zero crossing
	auto din = GetAnalogInputWaveform(0);
	float midpoint = GetAvgVoltage(din);

	//Timestamps of the edges
	vector<int64_t> edges;
	FindZeroCrossings(din, midpoint, edges);
	if(edges.size() < 2)
	{
		SetData(NULL, 0);
		return;
	}

	//Create the output
	auto cap = new AnalogWaveform;

	for(size_t i=0; i < (edges.size()-2); i+= 2)
	{
		//measure from edge to 2 edges later, since we find all zero crossings regardless of polarity
		int64_t start = edges[i];
		int64_t end = edges[i+2];

		int64_t delta = end - start;
		cap->m_offsets.push_back(start);
		cap->m_durations.push_back(delta);
		cap->m_samples.push_back(delta);
	}

	SetData(cap, 0);

	//Copy start time etc from the input. Timestamps are in femtoseconds.
	cap->m_timescale = 1;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
}
