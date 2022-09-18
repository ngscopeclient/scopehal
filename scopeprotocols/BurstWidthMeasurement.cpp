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
#include "BurstWidthMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

BurstWidthMeasurement::BurstWidthMeasurement(const string& color)
	: Filter(color, CAT_MEASUREMENT)
{
	AddStream(Unit(Unit::UNIT_FS), "data", Stream::STREAM_TYPE_ANALOG);

	//Set up channels
	CreateInput("din");

	m_idletime = "Idle Time";
	m_parameters[m_idletime] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));
	m_parameters[m_idletime].SetIntVal(1000000000000);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool BurstWidthMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if(i > 0)
		return false;

	if( (stream.GetType() == Stream::STREAM_TYPE_ANALOG) ||
		(stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
	{
		return true;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string BurstWidthMeasurement::GetProtocolName()
{
	return "BurstWidth";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void BurstWidthMeasurement::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	auto din = GetInputWaveform(0);
	din->PrepareForCpuAccess();
	auto uadin = dynamic_cast<UniformAnalogWaveform*>(din);
	auto sadin = dynamic_cast<SparseAnalogWaveform*>(din);
	auto uddin = dynamic_cast<UniformDigitalWaveform*>(din);
	auto sddin = dynamic_cast<SparseDigitalWaveform*>(din);
	vector<int64_t> edges;

	//Auto-threshold analog signals at 50% of full scale range
	if(uadin)
		FindZeroCrossings(uadin, GetAvgVoltage(uadin), edges);
	else if(sadin)
		FindZeroCrossings(sadin, GetAvgVoltage(sadin), edges);

	//Just find edges in digital signals
	else if(uddin)
		FindZeroCrossings(uddin, edges);
	else
		FindZeroCrossings(sddin, edges);

	//We need at least one full cycle of the waveform to have a meaningful burst width
	if(edges.size() < 2)
	{
		SetData(NULL, 0);
		return;
	}

	//Create the output
	auto cap = SetupEmptySparseAnalogOutputWaveform(din, 0, true);
	cap->m_timescale = 1;
	cap->PrepareForCpuAccess();

	//Add an edge at the extreme end to detect end condition
	edges.push_back(0xFFFFFFFFFFFFFFFF);

	size_t elen = edges.size();
	int64_t start = edges[0];
	int64_t e1 = edges[0];
	int64_t e2 = edges[1];

	//Get the idle time to look for. A burst will be detected when difference
	//between two consecutive edges is greater than the idle time
	int64_t idletime = m_parameters[m_idletime].GetIntVal();

	for(size_t i = 0; i < (elen - 1); i++)
	{
		//Search for a burst or end of waveform
		while(((e2 - e1) < idletime) && (i < (elen - 1)))
		{
			e1 = edges[i];
			e2 = edges[i + 1];
			i++;
		}

		//Push the burst width
		cap->m_offsets.push_back(start);
		cap->m_durations.push_back(e1 - start);
		cap->m_samples.push_back(e1 - start);
		
		//Move edges forward to detect any new burst
		if (i < (elen - 2))
		{
			start = e2;
			e1 = e2;
			e2 = edges[i + 2];
		}
	}

	if (cap->size() == 0)
	{
		cap->m_offsets.push_back(0);
		cap->m_durations.push_back(0);
		cap->m_samples.push_back(0);
	}

	SetData(cap, 0);

	cap->MarkModifiedFromCpu();
}
