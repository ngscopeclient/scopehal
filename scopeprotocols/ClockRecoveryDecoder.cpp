
/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2019 Andrew D. Zonenberg                                                                          *
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
#include "ClockRecoveryDecoder.h"
#include "../scopehal/DigitalRenderer.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ClockRecoveryDecoder::ClockRecoveryDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_DIGITAL, color, CAT_SERIAL)
{
	//Set up channels
	m_signalNames.push_back("IN");
	m_channels.push_back(NULL);

	m_baudname = "Symbol rate";
	m_parameters[m_baudname] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_INT);
	m_parameters[m_baudname].SetIntVal(1250000000);	//1250 MHz by default
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

ChannelRenderer* ClockRecoveryDecoder::CreateRenderer()
{
	return new DigitalRenderer(this);
}

bool ClockRecoveryDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void ClockRecoveryDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "ClockRec(%s)", m_channels[0]->m_displayname.c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string ClockRecoveryDecoder::GetProtocolName()
{
	return "Clock Recovery";
}

bool ClockRecoveryDecoder::IsOverlay()
{
	//we're an overlaid digital channel
	return true;
}

bool ClockRecoveryDecoder::NeedsConfig()
{
	//we have need the base symbol rate configured
	return true;
}

double ClockRecoveryDecoder::GetVoltageRange()
{
	//ignored
	return 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void ClockRecoveryDecoder::Refresh()
{
	//Get the input data
	if(m_channels[0] == NULL)
	{
		SetData(NULL);
		return;
	}

	AnalogCapture* din = dynamic_cast<AnalogCapture*>(m_channels[0]->GetData());
	if( (din == NULL) || (din->GetDepth() == 0) )
	{
		SetData(NULL);
		return;
	}

	//Look up the nominal baud rate and convert to time
	int64_t baud = m_parameters[m_baudname].GetIntVal();
	int64_t ps = static_cast<int64_t>(1.0e12f / baud);

	//Create the output waveform and copy our timescales
	DigitalCapture* cap = new DigitalCapture;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startPicoseconds = din->m_startPicoseconds;
	//cap->m_timescale = din->m_timescale;
	//cap->m_triggerPhase = din->m_triggerPhase;
	cap->m_triggerPhase = 0;
	cap->m_timescale = 1;		//recovered clock time scale is single picoseconds

	//Timestamps of the edges
	vector<int64_t> edges;

	//Find times of the zero crossings
	bool first = true;
	bool last = false;
	const float threshold = 0;
	for(size_t i=1; i<din->m_samples.size(); i++)
	{
		auto sin = din->m_samples[i];
		bool value = static_cast<float>(sin) > threshold;

		//Start time of the sample, in picoseconds
		int64_t t = din->m_triggerPhase + din->m_timescale * sin.m_offset;

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
		t += din->m_timescale * Measurement::InterpolateTime(din, i-1, threshold);
		edges.push_back(t);
		last = value;
	}

	//The actual PLL NCO
	//TODO: use the real fibre channel PLL.
	int64_t tend = din->m_samples[din->m_samples.size() - 1].m_offset * din->m_timescale;
	float period = ps;
	size_t nedge = 1;
	LogDebug("n,period,phase_error\n");
	double edgepos = (edges[0] + period/2);
	bool value = false;
	for(; (edgepos < tend) && (nedge < edges.size()-1); edgepos += period)
	{
		float center = period/2;

		//See if the next edge occurred in this UI.
		//If not, just run the NCO open loop.
		//Allow multiple edges in the UI if the frequency is way off.
		int64_t tnext = edges[nedge];
		while(tnext < edgepos)
		{
			//Find phase error
			int64_t delta = edgepos - tnext;
			int64_t phase_error = center - delta;

			//Check sign of phase and do bang-bang feedback (constant shift regardless of error magnitude)
			if(phase_error < 0)
			{
				period -= 0.02;
				edgepos -= 0.5;
			}
			else
			{
				period += 0.02;
				edgepos += 0.5;
			}

			LogDebug("%ld,%.2f,%ld\n", nedge, period, phase_error);
			tnext = edges[++nedge];
		}

		//Add the sample
		value = !value;
		cap->m_samples.push_back(DigitalSample((int64_t)edgepos, (int64_t)period, value));
	}

	SetData(cap);
}
