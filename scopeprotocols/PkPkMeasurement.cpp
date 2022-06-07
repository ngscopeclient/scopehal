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
#include "PkPkMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PkPkMeasurement::PkPkMeasurement(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MEASUREMENT)
{
	//Set up channels
	CreateInput("din");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool PkPkMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string PkPkMeasurement::GetProtocolName()
{
	return "Peak-To-Peak";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void PkPkMeasurement::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din = GetAnalogInputWaveform(0);
	size_t len = din->m_samples.size();

	//Copy Y axis units from input
	SetYAxisUnits(m_inputs[0].GetYAxisUnits(), 0);

	//Figure out the nominal midpoint of the waveform
	float top = GetTopVoltage(din);
	float base = GetBaseVoltage(din);
	float midpoint = (top+base)/2;

	//Create the output
	auto cap = new AnalogWaveform;

	int64_t		tmin		= 0;
	float		vmin		= FLT_MAX;
	float		vmax		= -FLT_MAX;
	float		last_max	= -FLT_MAX;

	//For each cycle, find the min and max
	bool		last_was_low	= true;
	bool		first			= true;
	for(size_t i=0; i < len; i++)
	{
		//If we're above the midpoint, reset everything and add a new sample
		float v = din->m_samples[i];
		if(v > midpoint)
		{
			last_was_low = false;

			//Add a sample for the current value (if any)
			if( (tmin > 0) && (last_max > -FLT_MAX/2) )
			{
				//Update duration of the previous sample
				size_t off = cap->m_offsets.size();
				if(off > 0)
					cap->m_durations[off-1] = tmin - cap->m_offsets[off-1];

				float value = last_max - vmin;

				//Add the new sample
				//Discard the first cycle as it might be incomplete
				if(first)
					first = false;
				else
				{
					cap->m_offsets.push_back(tmin);
					cap->m_durations.push_back(0);
					cap->m_samples.push_back(value);
				}
			}

			//Reset
			tmin = 0;
			vmin = FLT_MAX;

			//Accumulate
			vmax = max(vmax, v);
		}

		//Accumulate the lowest peak of this cycle
		//and save it
		else
		{
			if(!last_was_low)
			{
				last_max = vmax;
				vmax = -FLT_MAX;
				last_was_low = true;
			}

			if(v < vmin)
			{
				tmin = din->m_offsets[i];
				vmin = v;
			}
		}
	}

	SetData(cap, 0);

	//Copy start time etc from the input.
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
}
