/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2023 Andrew D. Zonenberg and contributors                                                         *
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
#include "TopMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

TopMeasurement::TopMeasurement(const string& color)
	: Filter(color, CAT_MEASUREMENT)
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput("din");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool TopMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string TopMeasurement::GetProtocolName()
{
	return "Top";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void TopMeasurement::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din = GetInputWaveform(0);
	din->PrepareForCpuAccess();
	auto sdin = dynamic_cast<SparseAnalogWaveform*>(din);
	auto udin = dynamic_cast<UniformAnalogWaveform*>(din);
	size_t len = din->size();

	//Make a histogram of the waveform
	float min = GetMinVoltage(sdin, udin);
	float max = GetMaxVoltage(sdin, udin);
	size_t nbins = 64;
	vector<size_t> hist = MakeHistogram(sdin, udin, min, max, nbins);

	//Set temporary midpoint and range
	float range = (max - min);
	float midpoint = range/2 + min;

	//Find the highest peak in the last quarter of the histogram
	//This is the peak for the entire waveform
	size_t binval = 0;
	size_t idx = 0;
	for(size_t i=(nbins*3/4); i<nbins; i++)
	{
		if(hist[i] > binval)
		{
			binval = hist[i];
			idx = i;
		}
	}
	float fbin = (idx + 0.5f)/nbins;
	float global_top = fbin*range + min;

	//Create the output
	auto cap = SetupEmptySparseAnalogOutputWaveform(din, 0);
	cap->m_timescale = 1;
	cap->PrepareForCpuAccess();

	float last = min;
	int64_t tedge = 0;
	float sum = 0;
	int64_t count = 0;
	float delta = range * 0.1;

	for(size_t i=0; i < len; i++)
	{
		//Wait for a rising edge
		float cur = GetValue(sdin, udin, i);
		int64_t tnow = ::GetOffsetScaled(sdin, udin, i);

		if( (cur > midpoint) && (last <= midpoint) )
		{
			//Done, add the sample
			if(count != 0)
			{
				cap->m_offsets.push_back(tedge);
				cap->m_durations.push_back(tnow - tedge);
				cap->m_samples.push_back(sum / count);
			}
			tedge = tnow;
		}

		//If the value is fairly close to the calculated top, average it
		//TODO: discard samples on the rising/falling edges as this will skew the results
		if(fabs(cur - global_top) < delta)
		{
			count ++;
			sum += cur;
		}

		last = cur;
	}

	SetData(cap, 0);

	cap->MarkModifiedFromCpu();
}
