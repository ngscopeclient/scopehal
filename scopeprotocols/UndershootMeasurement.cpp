/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2020 Andrew D. Zonenberg                                                                          *
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
#include "UndershootMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

UndershootMeasurement::UndershootMeasurement(string color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MEASUREMENT)
{
	//Set up channels
	CreateInput("din");

	m_midpoint = 0;
	m_range = 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool UndershootMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void UndershootMeasurement::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "Undershoot(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string UndershootMeasurement::GetProtocolName()
{
	return "Undershoot";
}

bool UndershootMeasurement::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool UndershootMeasurement::NeedsConfig()
{
	//automatic configuration
	return false;
}

double UndershootMeasurement::GetVoltageRange()
{
	return m_range;
}

double UndershootMeasurement::GetOffset()
{
	return -m_midpoint;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void UndershootMeasurement::Refresh()
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

	//Figure out the nominal top of the waveform
	float top = GetTopVoltage(din);
	float base = GetBaseVoltage(din);
	float midpoint = (top+base)/2;

	//Create the output
	auto cap = new AnalogWaveform;

	float 		fmax = -FLT_MAX;
	float		fmin =  FLT_MAX;

	int64_t		tmin = 0;
	float		vmin = FLT_MAX;

	//For each cycle, find how far we got below the base
	for(size_t i=0; i < len; i++)
	{
		//If we're above the midpoint, reset everything and add a new sample
		float v = din->m_samples[i];
		if(v > midpoint)
		{
			//Add a sample for the current value (if any)
			if(tmin > 0)
			{
				//Update duration of the previous sample
				size_t off = cap->m_offsets.size();
				if(off > 0)
					cap->m_durations[off-1] = tmin - cap->m_offsets[off-1];

				float value = base - vmin;
				fmax = max(fmax, value);
				fmin = min(fmin, value);

				//Add the new sample
				cap->m_offsets.push_back(tmin);
				cap->m_durations.push_back(0);
				cap->m_samples.push_back(value);
			}

			//Reset
			tmin = 0;
			vmin = FLT_MAX;
		}

		//Accumulate the lowest peak of this cycle
		else
		{
			if(v < vmin)
			{
				tmin = din->m_offsets[i];
				vmin = v;
			}
		}
	}

	m_range = fmax - fmin;
	if(m_range < 0.025)
		m_range = 0.025;
	m_midpoint = (fmax + fmin) / 2;

	SetData(cap, 0);

	//Copy start time etc from the input.
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startPicoseconds = din->m_startPicoseconds;
}
