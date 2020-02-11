
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
#include "ClockRecoveryDecoder.h"
#include "../scopehal/DigitalRenderer.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ClockRecoveryDecoder::ClockRecoveryDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_DIGITAL, color, CAT_CLOCK)
{
	//Set up channels
	m_signalNames.push_back("IN");
	m_channels.push_back(NULL);

	m_signalNames.push_back("Gate");	//leave null if not gating
	m_channels.push_back(NULL);

	m_baudname = "Symbol rate";
	m_parameters[m_baudname] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_INT);
	m_parameters[m_baudname].SetIntVal(1250000000);	//1250 MHz by default

	m_threshname = "Threshold";
	m_parameters[m_threshname] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_FLOAT);
	m_parameters[m_threshname].SetFloatVal(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

ChannelRenderer* ClockRecoveryDecoder::CreateRenderer()
{
	return new DigitalRenderer(this);
}

bool ClockRecoveryDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	switch(i)
	{
		case 0:
			return (channel != NULL) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG);

		case 1:
			return ( (channel == NULL) || (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) );

		default:
			return false;
	}
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
	return "Clock Recovery (PLL)";
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

	DigitalCapture* gate = NULL;
	if(m_channels[1] != NULL)
		gate = dynamic_cast<DigitalCapture*>(m_channels[1]->GetData());

	//Look up the nominal baud rate and convert to time
	int64_t baud = m_parameters[m_baudname].GetIntVal();
	int64_t ps = static_cast<int64_t>(1.0e12f / baud);

	//Create the output waveform and copy our timescales
	DigitalCapture* cap = new DigitalCapture;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startPicoseconds = din->m_startPicoseconds;
	cap->m_triggerPhase = 0;
	cap->m_timescale = 1;		//recovered clock time scale is single picoseconds

	double start = GetTime();

	//Timestamps of the edges
	vector<int64_t> edges;
	FindZeroCrossings(din, m_parameters[m_threshname].GetFloatVal(), edges);

	if(edges.empty())
	{
		SetData(NULL);
		return;
	}

	double dt = GetTime() - start;
	start = GetTime();
	//LogTrace("Zero crossing: %.3f ms\n", dt * 1000);

	//The actual PLL NCO
	//TODO: use the real fibre channel PLL.
	int64_t tend = din->m_samples[din->m_samples.size() - 1].m_offset * din->m_timescale;
	float period = ps;
	size_t nedge = 1;
	//LogDebug("n,delta,period\n");
	double edgepos = edges[0];
	bool value = false;
	double total_error = 0;
	cap->m_samples.reserve(edges.size());
	size_t igate = 0;
	bool gating = false;
	for(; (edgepos < tend) && (nedge < edges.size()-1); edgepos += period)
	{
		float center = period/2;
		double edgepos_orig = edgepos;

		//See if the current edge position is within a gating region
		bool was_gating = gating;
		if(gate != NULL)
		{
			while(igate < edges.size()-1)
			{
				//See if this edge is within the region
				int64_t a = gate->m_samples[igate].m_offset;
				int64_t b = a + gate->m_samples[igate].m_duration;
				a *= gate->m_timescale;
				b *= gate->m_timescale;

				//We went too far, stop
				if(edgepos < a)
					break;

				//Keep looking
				else if(edgepos > b)
					igate ++;

				//Good alignment
				else
				{
					gating = !gate->m_samples[igate].m_sample;
					break;
				}
			}
		}

		//See if the next edge occurred in this UI.
		//If not, just run the NCO open loop.
		//Allow multiple edges in the UI if the frequency is way off.
		int64_t tnext = edges[nedge];
		while(tnext + center < edgepos)
		{
			//Find phase error
			int64_t delta = (edgepos - tnext) - period;
			total_error += fabs(delta);

			//If the clock is currently gated, re-sync to the edge
			if(was_gating && !gating)
			{
				edgepos = tnext + period;
				delta = 0;
			}

			//Check sign of phase and do bang-bang feedback (constant shift regardless of error magnitude)
			else if(delta > 0)
			{
				period  -= 0.00005 * period;
				edgepos -= 0.005 * period;
			}
			else
			{
				period  += 0.00005 * period;
				edgepos += 0.005 * period;
			}

			//LogDebug("%ld,%ld,%.2f\n", nedge, delta, period);
			tnext = edges[++nedge];
		}

		//Add the sample
		if(!gating)
		{
			value = !value;
			cap->m_samples.push_back(DigitalSample(
				static_cast<int64_t>(round(edgepos_orig + period/2 - din->m_timescale*1.5)),
				(int64_t)period, value));
		}
	}

	dt = GetTime() - start;
	start = GetTime();
	LogTrace("NCO: %.3f ms\n", dt * 1000);

	total_error /= edges.size();
	LogTrace("average phase error %.1f\n", total_error);

	SetData(cap);
}
