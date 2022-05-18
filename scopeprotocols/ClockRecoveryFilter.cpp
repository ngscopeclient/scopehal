/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2021 Andrew D. Zonenberg and contributors                                                         *
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
#include "scopeprotocols.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ClockRecoveryFilter::ClockRecoveryFilter(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_DIGITAL, color, CAT_CLOCK)
{
	//Set up channels
	CreateInput("IN");
	CreateInput("Gate");

	m_baudname = "Symbol rate";
	m_parameters[m_baudname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ));
	m_parameters[m_baudname].SetFloatVal(1250000000);	//1.25 Gbps

	m_threshname = "Threshold";
	m_parameters[m_threshname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_threshname].SetFloatVal(0);
}

ClockRecoveryFilter::~ClockRecoveryFilter()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool ClockRecoveryFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	switch(i)
	{
		case 0:
			if(stream.m_channel == NULL)
				return false;
			return
				(stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) ||
				(stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL);

		case 1:
			if(stream.m_channel == NULL)	//null is legal for gate
				return true;

			return (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL);

		default:
			return false;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string ClockRecoveryFilter::GetProtocolName()
{
	return "Clock Recovery (PLL)";
}

bool ClockRecoveryFilter::NeedsConfig()
{
	//we have need the base symbol rate configured
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void ClockRecoveryFilter::Refresh()
{
	//Require a data signal, but not necessarily a gate
	if(!VerifyInputOK(0))
	{
		SetData(NULL, 0);
		return;
	}

	auto adin = GetAnalogInputWaveform(0);
	auto ddin = GetDigitalInputWaveform(0);
	auto gate = GetDigitalInputWaveform(1);

	//Timestamps of the edges
	vector<int64_t> edges;
	if(adin)
		FindZeroCrossings(adin, m_parameters[m_threshname].GetFloatVal(), edges);
	else
		FindZeroCrossings(ddin, edges);
	if(edges.empty())
	{
		SetData(NULL, 0);
		return;
	}

	//Get nominal period used for the first cycle of the NCO
	int64_t period = round(FS_PER_SECOND / m_parameters[m_baudname].GetFloatVal());

	//Create the output waveform and copy our timescales
	auto cap = new DigitalWaveform;
	if(adin)
	{
		cap->m_startTimestamp = adin->m_startTimestamp;
		cap->m_startFemtoseconds = adin->m_startFemtoseconds;
	}
	else
	{
		cap->m_startTimestamp = ddin->m_startTimestamp;
		cap->m_startFemtoseconds = ddin->m_startFemtoseconds;
	}
	cap->m_triggerPhase = 0;
	cap->m_timescale = 1;		//recovered clock time scale is single femtoseconds

	//The actual PLL NCO
	//TODO: use the real fibre channel PLL.
	int64_t tend;
	if(adin)
		tend = adin->m_offsets[adin->m_offsets.size() - 1] * adin->m_timescale;
	else
		tend = ddin->m_offsets[ddin->m_offsets.size() - 1] * ddin->m_timescale;
	size_t nedge = 1;
	//LogDebug("n, delta, period, freq_ghz\n");
	int64_t edgepos = edges[0];
	bool value = false;
	int64_t total_error = 0;
	cap->m_samples.reserve(edges.size());
	size_t igate = 0;
	bool gating = false;
	int cycles_open_loop = 0;
	for(; (edgepos < tend) && (nedge < edges.size()-1); edgepos += period)
	{
		float center = period/2;

		//See if the current edge position is within a gating region
		bool was_gating = gating;
		if(gate != NULL)
		{
			while(igate < edges.size()-1)
			{
				//See if this edge is within the region
				int64_t a = gate->m_offsets[igate];
				int64_t b = a + gate->m_durations[igate];
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
					gating = !gate->m_samples[igate];
					break;
				}
			}
		}

		//See if the next edge occurred in this UI.
		//If not, just run the NCO open loop.
		//Allow multiple edges in the UI if the frequency is way off.
		int64_t tnext = edges[nedge];
		cycles_open_loop ++;
		while( (tnext + center < edgepos) && (nedge+1 < edges.size()) )
		{
			//Find phase error
			int64_t delta = (edgepos - tnext) - period;
			total_error += fabs(delta);

			//If the clock is currently gated, re-sync to the edge
			if(was_gating && !gating)
				edgepos = tnext + period;

			//Check sign of phase and do bang-bang feedback (constant shift regardless of error magnitude)
			//If we skipped some edges, apply a larger correction
			else
			{
				int64_t cperiod = period * cycles_open_loop;
				if(delta > 0)
				{
					period  -= cperiod / 40000;
					edgepos -= cperiod / 400;
				}
				else
				{
					period  += cperiod / 40000;
					edgepos += cperiod / 400;
				}
			}

			cycles_open_loop = 0;

			//LogDebug("%ld,%f,%.2f, %.4f\n", nedge, delta, period, 1e3f / period);
			tnext = edges[++nedge];
		}

		//Add the sample
		if(!gating)
		{
			value = !value;

			cap->m_offsets.push_back(edgepos + period/2);
			cap->m_durations.push_back(period);
			cap->m_samples.push_back(value);
		}
	}

	total_error /= edges.size();
	LogTrace("average phase error %zu\n", total_error);

	SetData(cap, 0);
}
