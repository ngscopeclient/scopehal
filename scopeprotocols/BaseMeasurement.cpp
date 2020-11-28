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
#include "BaseMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

BaseMeasurement::BaseMeasurement(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MEASUREMENT)
{
	CreateInput("din");

	m_midpoint = 0;
	m_range = 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool BaseMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void BaseMeasurement::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "Base(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string BaseMeasurement::GetProtocolName()
{
	return "Base";
}

bool BaseMeasurement::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool BaseMeasurement::NeedsConfig()
{
	//automatic configuration
	return false;
}

double BaseMeasurement::GetVoltageRange()
{
	return m_range;
}

double BaseMeasurement::GetOffset()
{
	return -m_midpoint;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void BaseMeasurement::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	auto din = GetAnalogInputWaveform(0);
	size_t len = din->m_samples.size();

	//Make a histogram of the waveform
	float min = GetMinVoltage(din);
	float max = GetMaxVoltage(din);
	size_t nbins = 64;
	vector<size_t> hist = MakeHistogram(din, min, max, nbins);

	//Set temporary midpoint and range
	m_range = (max - min);
	m_midpoint = m_range/2 + min;

	//Find the highest peak in the first quarter of the histogram
	//This is the base for the entire waveform
	size_t binval = 0;
	size_t idx = 0;
	for(size_t i=0; i<(nbins/4); i++)
	{
		if(hist[i] > binval)
		{
			binval = hist[i];
			idx = i;
		}
	}
	float fbin = (idx + 0.5f)/nbins;
	float global_base = fbin*m_range + min;

	//Create the output
	auto cap = new AnalogWaveform;

	float last = min;
	int64_t tedge = 0;
	float sum = 0;
	int64_t count = 0;
	float delta = m_range * 0.1;

	float fmax = -99999;
	float fmin =  99999;

	for(size_t i=0; i < len; i++)
	{
		//Wait for a falling edge
		float cur = din->m_samples[i];
		int64_t tnow = din->m_offsets[i] * din->m_timescale;

		if( (cur < m_midpoint) && (last >= m_midpoint) )
		{
			//Done, add the sample
			if(count != 0)
			{
				float vavg = sum/count;
				if(vavg > fmax)
					fmax = vavg;
				if(vavg < fmin)
					fmin = vavg;

				cap->m_offsets.push_back(tedge);
				cap->m_durations.push_back(tnow - tedge);
				cap->m_samples.push_back(vavg);
			}
			tedge = tnow;
		}

		//If the value is fairly close to the calculated base, average it
		//TODO: discard samples on the rising/falling edges as this will skew the results
		if(fabs(cur - global_base) < delta)
		{
			count ++;
			sum += cur;
		}

		last = cur;
	}

	m_range = fmax - fmin;
	if(m_range < 0.025)
		m_range = 0.025;
	m_midpoint = (fmax + fmin) / 2;

	SetData(cap, 0);

	//Copy start time etc from the input. Timestamps are in femtoseconds.
	cap->m_timescale = 1;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
}
