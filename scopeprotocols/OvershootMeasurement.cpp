/************************************************************************************************************************                                                                                                                      *
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
#include "OvershootMeasurement.h"
#include "../scopehal/KahanSummation.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

OvershootMeasurement::OvershootMeasurement(const string& color)
	: Filter(color, CAT_MEASUREMENT)
{
	AddStream(Unit(Unit::UNIT_VOLTS), "trend", Stream::STREAM_TYPE_ANALOG);
	AddStream(Unit(Unit::UNIT_VOLTS), "avg", Stream::STREAM_TYPE_ANALOG_SCALAR);
	AddStream(Unit(Unit::UNIT_VOLTS), "max", Stream::STREAM_TYPE_ANALOG_SCALAR);

	CreateInput("din");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool OvershootMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string OvershootMeasurement::GetProtocolName()
{
	return "Overshoot";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void OvershootMeasurement::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(nullptr, 0);
		return;
	}

	//Get the input data
	auto din = GetInputWaveform(0);
	din->PrepareForCpuAccess();
	size_t len = din->size();

	auto sdin = dynamic_cast<SparseAnalogWaveform*>(din);
	auto udin = dynamic_cast<UniformAnalogWaveform*>(din);

	//Figure out the nominal top of the waveform
	float top = GetTopVoltage(sdin, udin);
	float base = GetBaseVoltage(sdin, udin);
	float midpoint = (top+base)/2;

	//Create the output
	auto cap = SetupEmptySparseAnalogOutputWaveform(din, 0);
	cap->PrepareForCpuAccess();

	int64_t		tmax = 0;
	float		vmax = 0;
	float		globalmax = -FLT_MAX;

	//For each cycle, find how far we got above the top
	KahanSummation sum;
	size_t nedges = 0;
	for(size_t i=0; i < len; i++)
	{
		//If we're below the midpoint, reset everything and add a new sample
		float v = GetValue(sdin, udin, i);
		if(v < midpoint)
		{
			//Add a sample for the current value (if any)
			if(tmax > 0)
			{
				//Update duration of the previous sample
				size_t off = cap->size();
				if(off > 0)
					cap->m_durations[off-1] = tmax - cap->m_offsets[off-1];

				//Add the new sample
				float overshoot = vmax - top;
				cap->m_offsets.push_back(tmax);
				cap->m_durations.push_back(0);
				cap->m_samples.push_back(overshoot);

				nedges ++;
				globalmax = max(overshoot, globalmax);
				sum += overshoot;
			}

			//Reset
			tmax = 0;
			vmax = -FLT_MAX;
		}

		//Accumulate the highest peak of this cycle
		else
		{
			if(v > vmax)
			{
				tmax = ::GetOffset(sdin, udin, i);
				vmax = v;
			}
		}
	}
	SetData(cap, 0);

	m_streams[1].m_value = sum.GetSum() / nedges;
	m_streams[2].m_value = globalmax;

	cap->MarkModifiedFromCpu();
}
