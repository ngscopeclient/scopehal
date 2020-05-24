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
#include "PkPkMeasurementDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PkPkMeasurementDecoder::PkPkMeasurementDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MEASUREMENT)
{
	//Set up channels
	m_signalNames.push_back("din");
	m_channels.push_back(NULL);

	m_midpoint = 0;
	m_range = 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool PkPkMeasurementDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void PkPkMeasurementDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "PkPk(%s)", m_channels[0]->m_displayname.c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string PkPkMeasurementDecoder::GetProtocolName()
{
	return "Peak-to-Peak";
}

bool PkPkMeasurementDecoder::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool PkPkMeasurementDecoder::NeedsConfig()
{
	//automatic configuration
	return false;
}

double PkPkMeasurementDecoder::GetVoltageRange()
{
	return m_range;
}

double PkPkMeasurementDecoder::GetOffset()
{
	return -m_midpoint;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void PkPkMeasurementDecoder::Refresh()
{
	//Get the input data
	if(m_channels[0] == NULL)
	{
		SetData(NULL);
		return;
	}
	auto din = dynamic_cast<AnalogWaveform*>(m_channels[0]->GetData());
	if(din == NULL)
	{
		SetData(NULL);
		return;
	}

	//We need meaningful data
	size_t len = din->m_samples.size();
	if(len == 0)
	{
		SetData(NULL);
		return;
	}

	//Figure out the nominal midpoint of the waveform
	float top = Measurement::GetTopVoltage(din);
	float base = Measurement::GetBaseVoltage(din);
	float midpoint = (top+base)/2;

	//Create the output
	auto cap = new AnalogWaveform;

	float 		fmax = -FLT_MAX;
	float		fmin =  FLT_MAX;

	int64_t		tmin		= 0;
	float		vmin		= FLT_MAX;
	float		vmax		= -FLT_MAX;
	float		last_max	= -FLT_MAX;

	//For each cycle, find the min and max
	bool		last_was_low	= true;
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

			//Accumulate
			vmax = max(vmax, v);
		}

		//Accumulate the lowest peak of this cycle
		//and save the
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

	m_range = fmax - fmin;
	if(m_range < 0.025)
		m_range = 0.025;
	m_midpoint = (fmax + fmin) / 2;

	SetData(cap);

	//Copy start time etc from the input.
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startPicoseconds = din->m_startPicoseconds;
}
