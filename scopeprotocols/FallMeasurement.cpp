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
#include "FallMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

FallMeasurement::FallMeasurement(const string& color)
	: Filter(color, CAT_MEASUREMENT)
{
	//Set up channels
	CreateInput("din");
	AddStream(Unit(Unit::UNIT_FS), "data", Stream::STREAM_TYPE_ANALOG);

	m_startname = "Start Fraction";
	m_parameters[m_startname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_PERCENT));
	m_parameters[m_startname].SetFloatVal(0.8);

	m_endname = "End Fraction";
	m_parameters[m_endname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_PERCENT));
	m_parameters[m_endname].SetFloatVal(0.2);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool FallMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string FallMeasurement::GetProtocolName()
{
	return "Fall";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void FallMeasurement::Refresh()
{
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din = GetInputWaveform(0);
	auto sdin = dynamic_cast<SparseAnalogWaveform*>(din);
	auto udin = dynamic_cast<UniformAnalogWaveform*>(din);
	din->PrepareForCpuAccess();
	size_t len = din->size();

	//Get the base/top (we use these for calculating percentages)
	float base = GetBaseVoltage(sdin, udin);
	float top = GetTopVoltage(sdin, udin);

	//Find the actual levels we use for our time gate
	float delta = top - base;
	float vstart = base + m_parameters[m_startname].GetFloatVal()*delta;
	float vend = base + m_parameters[m_endname].GetFloatVal()*delta;

	//Create the output
	auto cap = SetupEmptySparseAnalogOutputWaveform(din, 0, true);
	cap->PrepareForCpuAccess();
	cap->m_timescale = 1;

	float last = -1e20;
	double tedge = 0;

	int state = 0;
	int64_t tlast = 0;

	//LogDebug("vstart = %.3f, vend = %.3f\n", vstart, vend);
	for(size_t i=0; i < len; i++)
	{
		float cur = GetValue(sdin, udin, i);
		int64_t tnow = ::GetOffset(sdin, udin, i) * din->m_timescale;

		//Find start of edge
		if(state == 0)
		{
			if( (cur < vstart) && (last >= vstart) )
			{
				tedge = tnow - din->m_timescale + InterpolateTime(sdin, udin, i-1, vstart)*din->m_timescale;
				state = 1;
			}
		}

		//Find end of edge
		else if(state == 1)
		{
			if( (cur < vend) && (last >= vend) )
			{
				double dt = InterpolateTime(sdin, udin, i-1, vend)*din->m_timescale + tnow - din->m_timescale - tedge;

				cap->m_offsets.push_back(tlast);
				cap->m_durations.push_back(tnow - tlast);
				cap->m_samples.push_back(dt);
				tlast = tnow;

				state = 0;
			}
		}

		last = cur;
	}

	SetData(cap, 0);

	cap->MarkModifiedFromCpu();
}
