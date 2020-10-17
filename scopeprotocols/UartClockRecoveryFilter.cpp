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

#include "../scopehal/scopehal.h"
#include "UartClockRecoveryFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

UartClockRecoveryFilter::UartClockRecoveryFilter(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_DIGITAL, color, CAT_CLOCK)
{
	//Set up channels
	CreateInput("din");

	m_baudname = "Baud rate";
	m_parameters[m_baudname] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_BITRATE));
	m_parameters[m_baudname].SetIntVal(115200);	//115.2 Kbps by default

	m_threshname = "Threshold";
	m_parameters[m_threshname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_threshname].SetFloatVal(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool UartClockRecoveryFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void UartClockRecoveryFilter::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "UartClockRec(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string UartClockRecoveryFilter::GetProtocolName()
{
	return "Clock Recovery (UART)";
}

bool UartClockRecoveryFilter::IsOverlay()
{
	//we're an overlaid digital channel
	return true;
}

bool UartClockRecoveryFilter::NeedsConfig()
{
	//we have need the base symbol rate configured
	return true;
}

double UartClockRecoveryFilter::GetVoltageRange()
{
	//ignored
	return 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void UartClockRecoveryFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din = GetAnalogInputWaveform(0);

	//Look up the nominal baud rate and convert to time
	int64_t baud = m_parameters[m_baudname].GetIntVal();
	int64_t ps = static_cast<int64_t>(1.0e12f / baud);

	//Create the output waveform and copy our timescales
	auto cap = new DigitalWaveform;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startPicoseconds = din->m_startPicoseconds;
	cap->m_triggerPhase = 0;
	cap->m_timescale = 1;		//recovered clock time scale is single picoseconds

	//Timestamps of the edges
	vector<int64_t> edges;

	//Find times of the zero crossings
	bool first = true;
	bool last = false;
	const float threshold = m_parameters[m_threshname].GetFloatVal();
	size_t len = din->m_samples.size();
	for(size_t i=1; i<len; i++)
	{
		bool value = din->m_samples[i] > threshold;

		//Start time of the sample, in picoseconds
		int64_t t = din->m_triggerPhase + din->m_timescale * din->m_offsets[i];

		//Move to the middle of the sample
		t += din->m_timescale/2;

		//Save the last value
		if(first)
		{
			last = value;
			first = false;
			continue;
		}

		//Skip samples with no transition
		if(last == value)
			continue;

		//Interpolate the time
		t += din->m_timescale * InterpolateTime(din, i-1, threshold);
		edges.push_back(t);
		last = value;
	}

	//Actual DLL logic
	//TODO: recover from glitches better?
	size_t nedge = 0;
	int64_t bcenter = 0;
	bool value = false;
	size_t elen = edges.size();
	for(; nedge < elen;)
	{
		//The current bit starts half a baud period after the start bit edge
		bcenter = edges[nedge] + ps/2;
		nedge ++;

		//We have ten start/ data/stop bits after this
		for(int i=0; i<10; i++)
		{
			if(nedge >= elen)
				break;

			//If the next edge is around the time of this bit, re-sync to it
			if(edges[nedge] < bcenter + ps/4)
			{
				//bcenter = edges[nedge] + ps/2;
				nedge ++;
			}

			//Emit a sample for this data bit
			cap->m_offsets.push_back(bcenter);
			cap->m_durations.push_back(ps);
			cap->m_samples.push_back(value);
			value = !value;

			//Next bit starts one baud period later
			bcenter  += ps;
		}
	}

	SetData(cap, 0);
}
