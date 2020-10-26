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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Implementation of DPhySymbolDecoder
 */

#include "../scopehal/scopehal.h"
#include "DPhySymbolDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DPhySymbolDecoder::DPhySymbolDecoder(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_SERIAL)
{
	//Set up channels
	CreateInput("IN+");
	CreateInput("IN-");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DPhySymbolDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	//IN+ is required
	if(i == 0)
	{
		if(stream.m_channel == NULL)
			return false;
		return (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG);
	}

	//IN- can be omitted, but if not specified we can't decode all line states.
	//For many common interfaces, we can get away with this and save a probe.
	else if(i == 1)
	{
		if(stream.m_channel == NULL)
			return true;
		return (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG);
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string DPhySymbolDecoder::GetProtocolName()
{
	return "MIPI D-PHY Symbol";
}

void DPhySymbolDecoder::SetDefaultName()
{
	auto din1 = GetInput(1);

	char hwname[256];
	if(din1.m_channel == NULL)
		snprintf(hwname, sizeof(hwname), "DPHYSymbol(%s)", GetInputDisplayName(0).c_str());
	else
	{
		snprintf(hwname, sizeof(hwname), "DPHYSymbol(%s,%s)",
			GetInputDisplayName(0).c_str(),
			GetInputDisplayName(1).c_str());
	}
	m_hwname = hwname;
	m_displayname = m_hwname;
}

bool DPhySymbolDecoder::NeedsConfig()
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DPhySymbolDecoder::Refresh()
{
	//We need D+ no matter what
	if(!VerifyInputOK(0))
	{
		SetData(NULL, 0);
		return;
	}

	//Get D+ and D- data
	auto dp = GetAnalogInputWaveform(0);
	auto dn = GetAnalogInputWaveform(1);
	auto len = dp->m_samples.size();
	if(dn)
		len = min(len, dn->m_samples.size());

	//Create output waveform
	DPhySymbolWaveform* cap = new DPhySymbolWaveform;
	cap->m_timescale = dp->m_timescale;
	cap->m_startTimestamp = dp->m_startTimestamp;
	cap->m_startPicoseconds = dp->m_startPicoseconds;
	DPhySymbol::type state = DPhySymbol::STATE_LP00;

	/*
	If we have Dp only, we can decode a restricted subset of line states by cheating a bit.
	This isn't truly spec compliant but allows for protocol decoding with only one probe.

		HS-1
			Dp > 225 mV
			Dp < 880 mV

		HS-0
			Dp > 50 mV
			Dp < 175 mV

		LP-00 or LP-01 (decode as LP-00)
			Dp < 50 mV

		LP-10 or LP-11 (decode as LP-11)
			Dp > 880 mV
	*/
	if(!dn)
	{
		DPhySymbol::type nextstate = state;
		for(size_t i=0; i<len; i++)
		{
			float v = dp->m_samples[i];

			if(v > 0.88)
				nextstate = DPhySymbol::STATE_LP11;
			else if(v > 0.225)
				nextstate = DPhySymbol::STATE_HS1;
			else if(v < 0.005)
				nextstate = DPhySymbol::STATE_LP00;
			else if(v < 0.175)
				nextstate = DPhySymbol::STATE_HS0;

			//If same as existing state, extend it
			size_t nsize = cap->m_samples.size();
			size_t nlast = nsize-1;
			if(nsize && cap->m_samples[nlast].m_type == nextstate)
				cap->m_durations[nlast] = dp->m_offsets[i] + dp->m_durations[i] - cap->m_offsets[nlast];

			//Nope, create a new sample
			else
			{
				cap->m_offsets.push_back(dp->m_offsets[i]);
				cap->m_durations.push_back(dp->m_durations[i]);
				cap->m_samples.push_back(nextstate);
			}

			state = nextstate;
		}
	}

	else
	{
		DPhySymbol::type nextstate = state;
		for(size_t i=0; i<len; i++)
		{
			float vp = dp->m_samples[i];
			float vn = dn->m_samples[i];
			float vd = vp - vn;

			if( (vp < 0.55) && (vn < 0.55) )
			{
				//Can only go to a HS state from another HS state or LP00
				if( (state == DPhySymbol::STATE_HS0) ||
					(state == DPhySymbol::STATE_HS1) ||
					(state == DPhySymbol::STATE_LP00) )
				{
					if(vd < -0.07)
						nextstate = DPhySymbol::STATE_HS0;
					else if(vd > 0.07)
						nextstate = DPhySymbol::STATE_HS1;
				}

				if( (fabs(vd) < 0.07) && (vp < 0.15) && (vn < 0.15) )
					nextstate = DPhySymbol::STATE_LP00;
			}
			else if( (vp < 0.55) && (vn > 0.80) )
				nextstate = DPhySymbol::STATE_LP01;
			else if( (vp > 0.80) && (vn < 0.55) )
				nextstate = DPhySymbol::STATE_LP10;
			else if( (vp > 0.80) && (vn > 0.80) )
				nextstate = DPhySymbol::STATE_LP11;

			//If same as existing state, extend it
			size_t nsize = cap->m_samples.size();
			size_t nlast = nsize-1;
			if(nsize && cap->m_samples[nlast].m_type == nextstate)
				cap->m_durations[nlast] = dp->m_offsets[i] + dp->m_durations[i] - cap->m_offsets[nlast];

			//Nope, create a new sample
			else
			{
				//Glitch filter LP states.
				//If the previous sample was a LP state, but significantly less than Tlpx long, discard it.
				//For now, set the cutoff at 40 ns (40,000 ps)
				const int64_t tlpx_cutoff = 40000;
				if(nsize && (state != DPhySymbol::STATE_HS0) && (state != DPhySymbol::STATE_HS1) )
				{
					if( (cap->m_durations[nlast] * cap->m_timescale) < tlpx_cutoff )
					{
						cap->m_durations.resize(nlast);
						cap->m_offsets.resize(nlast);
						cap->m_samples.resize(nlast);

						//If there was a previous sample, extend it to the start of this one
						if(nsize > 1)
						{
							nlast --;
							cap->m_durations[nlast] = dp->m_offsets[i] + dp->m_durations[i] - cap->m_offsets[nlast];
						}
					}
				}

				cap->m_offsets.push_back(dp->m_offsets[i]);
				cap->m_durations.push_back(dp->m_durations[i]);
				cap->m_samples.push_back(nextstate);
			}

			state = nextstate;
		}
	}

	SetData(cap, 0);
}


Gdk::Color DPhySymbolDecoder::GetColor(int i)
{
	auto capture = dynamic_cast<DPhySymbolWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const DPhySymbol& s = capture->m_samples[i];

		switch(s.m_type)
		{
			case DPhySymbol::STATE_HS0:
			case DPhySymbol::STATE_HS1:
				return m_standardColors[COLOR_DATA];

			case DPhySymbol::STATE_LP00:
			case DPhySymbol::STATE_LP11:
			case DPhySymbol::STATE_LP01:
			case DPhySymbol::STATE_LP10:
				return m_standardColors[COLOR_CONTROL];
		}
	}

	return m_standardColors[COLOR_ERROR];
}

string DPhySymbolDecoder::GetText(int i)
{
	auto capture = dynamic_cast<DPhySymbolWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const DPhySymbol& s = capture->m_samples[i];

		switch(s.m_type)
		{
			case DPhySymbol::STATE_HS0:
				return "HS-0";

			case DPhySymbol::STATE_HS1:
				return "HS-1";

			case DPhySymbol::STATE_LP00:
				return "LP-00";

			case DPhySymbol::STATE_LP11:
				return "LP-11";

			case DPhySymbol::STATE_LP01:
				return "LP-01";

			case DPhySymbol::STATE_LP10:
				return "LP-10";
		}
	}
	return "Unknown";
}
