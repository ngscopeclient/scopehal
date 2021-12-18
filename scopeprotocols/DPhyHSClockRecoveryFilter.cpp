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
#include "DPhySymbolDecoder.h"
#include "DPhyHSClockRecoveryFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DPhyHSClockRecoveryFilter::DPhyHSClockRecoveryFilter(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_DIGITAL, color, CAT_CLOCK)
{
	//Set up channels
	CreateInput("clk");
	CreateInput("data");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DPhyHSClockRecoveryFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 2) && (dynamic_cast<DPhySymbolDecoder*>(stream.m_channel) != NULL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void DPhyHSClockRecoveryFilter::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "DPhyHSClockRec(%s, %s)",
		GetInputDisplayName(0).c_str(), GetInputDisplayName(1).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string DPhyHSClockRecoveryFilter::GetProtocolName()
{
	return "Clock Recovery (D-PHY HS Mode)";
}

bool DPhyHSClockRecoveryFilter::IsOverlay()
{
	//we're an overlaid digital channel
	return true;
}

bool DPhyHSClockRecoveryFilter::NeedsConfig()
{
	//we have more than one input
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DPhyHSClockRecoveryFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto clk = dynamic_cast<DPhySymbolWaveform*>(GetInputWaveform(0));
	auto data = dynamic_cast<DPhySymbolWaveform*>(GetInputWaveform(1));

	//Create the output waveform and copy our timescales
	auto cap = new DigitalWaveform;
	cap->m_startTimestamp = clk->m_startTimestamp;
	cap->m_startFemtoseconds = clk->m_startFemtoseconds;
	cap->m_triggerPhase = clk->m_triggerPhase;
	cap->m_timescale = clk->m_timescale;
	SetData(cap, 0);

	//Process the data
	size_t clklen = clk->m_samples.size();
	size_t datalen = data->m_samples.size();
	size_t iclk = 0;
	size_t idata = 0;
	int64_t timestamp	= 0;
	bool last_clk = false;
	int64_t tstart = 0;
	bool cur_out = false;
	while(true)
	{
		//Get the current samples
		auto cur_clk = clk->m_samples[iclk];
		auto cur_data = data->m_samples[idata];

		//Get timestamps of next event on each channel
		int64_t next_data = GetNextEventTimestamp(data, idata, datalen, timestamp);
		int64_t next_clk = GetNextEventTimestamp(clk, iclk, clklen, timestamp);
		int64_t next_timestamp = min(next_clk, next_data);
		if(next_timestamp == timestamp)
			break;

		//Look for clock edges
		bool clock_rising = false;
		bool clock_falling = false;
		if(cur_clk.m_type == DPhySymbol::STATE_HS1)
		{
			if(!last_clk)
				clock_rising = true;
			last_clk = true;
		}
		else if(cur_clk.m_type == DPhySymbol::STATE_HS0)
		{
			if(last_clk)
				clock_falling = true;
			last_clk = false;
		}
		bool clock_toggling = clock_rising || clock_falling;

		//See if the data is in HS mode
		bool hs_mode = (cur_data.m_type == DPhySymbol::STATE_HS0) || (cur_data.m_type == DPhySymbol::STATE_HS1);

		int64_t toff = clk->m_offsets[iclk];
		int64_t tend = toff + clk->m_durations[iclk];
		if(clock_toggling)
		{
			//Emit a new sample for this clock pulse if we have a toggle in HS mode
			if(hs_mode)
			{
				cap->m_offsets.push_back(tstart);
				cap->m_durations.push_back(tend - tstart);
				cap->m_samples.push_back(cur_out);

				cur_out = !cur_out;
				tstart = tend;
			}

			//If we've left HS mode, delete the last few toggles
			else
			{
				for(size_t i=0; i<10; i++)
				{
					if(cap->m_offsets.empty())
						break;

					cap->m_offsets.pop_back();
					cap->m_durations.pop_back();
					cap->m_samples.pop_back();
				}

				if(cap->m_offsets.empty())
				{
					tstart = 0;
					cur_out = false;
				}
				else
				{
					size_t n = cap->m_offsets.size() - 1;
					tstart = cap->m_offsets[n];
					cur_out = cap->m_samples[n];
				}
			}
		}

		//All good, move on
		timestamp = next_timestamp;
		AdvanceToTimestamp(clk, iclk, clklen, timestamp);
		AdvanceToTimestamp(data, idata, datalen, timestamp);
	}
}
