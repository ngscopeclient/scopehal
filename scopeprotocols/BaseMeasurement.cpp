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
#include "BaseMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

BaseMeasurement::BaseMeasurement(const string& color)
	: Filter(color, CAT_MEASUREMENT)
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput("din");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool BaseMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string BaseMeasurement::GetProtocolName()
{
	return "Base";
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
	float vmin = GetMinVoltage(din);
	float vmax = GetMaxVoltage(din);
	size_t nbins = 64;
	vector<size_t> hist = MakeHistogram(din, vmin, vmax, nbins);

	//Set temporary midpoint and range
	float range = (vmax - vmin);
	float mid = range/2 + vmin;

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
	float global_base = fbin*range + vmin;

	//Create the output
	auto cap = new AnalogWaveform;

	float last = vmin;
	int64_t tfall = 0;
	float delta = range * 0.1;

	float fmax = -FLT_MAX;
	float fmin =  FLT_MAX;

	bool first = true;

	vector<float> samples;

	for(size_t i=0; i < len; i++)
	{
		//Wait for a rising edge (end of the low period)
		float cur = din->m_samples[i];
		int64_t tnow = din->m_offsets[i] * din->m_timescale;

		//Find falling edge
		if( (cur < mid) && (last >= mid) )
			tfall = tnow;

		//Find rising edge
		if( (cur > mid) && (last <= mid) )
		{
			//Done, add the sample
			if(!samples.empty())
			{
				if(first)
					first = false;

				else
				{
					//Average the middle 50% of the samples.
					//Discard beginning and end as they include parts of the edge
					float sum = 0;
					int64_t count = 0;
					size_t start = samples.size()/4;
					size_t end = samples.size() - start;
					for(size_t j=start; j<=end; j++)
					{
						sum += samples[j];
						count ++;
					}

					float vavg = sum / count;

					fmax = max(fmax, vavg);
					fmin = min(fmin, vavg);

					int64_t tmid = (tnow + tfall) / 2;

					//Update duration for last sample
					size_t n = cap->m_samples.size();
					if(n)
						cap->m_durations[n-1] = tmid - cap->m_offsets[n-1];

					cap->m_offsets.push_back(tmid);
					cap->m_durations.push_back(1);
					cap->m_samples.push_back(vavg);
				}

				samples.clear();
			}
		}

		//If the value is fairly close to the calculated base, average it
		if(fabs(cur - global_base) < delta)
			samples.push_back(cur);

		last = cur;
	}

	SetData(cap, 0);

	//Copy start time etc from the input. Timestamps are in femtoseconds.
	cap->m_timescale = 1;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
}
