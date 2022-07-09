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
	: Filter(color, CAT_SERIAL)
{
	AddProtocolStream("data");
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
		return (stream.GetType() == Stream::STREAM_TYPE_ANALOG);
	}

	//IN- can be omitted, but if not specified we can't decode all line states.
	//For many common interfaces, we can get away with this and save a probe.
	else if(i == 1)
	{
		if(stream.m_channel == NULL)
			return true;
		return (stream.GetType() == Stream::STREAM_TYPE_ANALOG);
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string DPhySymbolDecoder::GetProtocolName()
{
	return "MIPI D-PHY Symbol";
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
	cap->m_timescale = 1;
	cap->m_startTimestamp = dp->m_startTimestamp;
	cap->m_startFemtoseconds = dp->m_startFemtoseconds;
	DPhySymbol::type last_state = DPhySymbol::STATE_HS0;
	DPhySymbol::type state = DPhySymbol::STATE_HS0;
	DPhySymbol::type nextstate = state;

	for(size_t i=0; i<len; i++)
	{
		float v = dp->m_samples[i];
		int64_t start = dp->m_offsets[i] * dp->m_timescale;
		int64_t dur = dp->m_durations[i] * dp->m_timescale;

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
			//Can only go to a HS state from another HS state or LP00
			if( (state == DPhySymbol::STATE_HS0) ||
				(state == DPhySymbol::STATE_HS1) )
			{
				if(v > 0.88)
					nextstate = DPhySymbol::STATE_LP11;
				else if(v > 0.21)
				{
					//Interpolate the toggle time to sub-sample precision
					if(i != 0)
						start += dp->m_timescale * InterpolateTime(dp, i-1, 0.21);

					nextstate = DPhySymbol::STATE_HS1;
				}
				else if(v < 0.16)
				{
					if(i != 0)
						start += dp->m_timescale * InterpolateTime(dp, i-1, 0.16);
					nextstate = DPhySymbol::STATE_HS0;
				}
			}

			//LP00 can go to HS0 or stay in LP00
			else if(state == DPhySymbol::STATE_LP00)
			{
				if(v > 0.125)
					nextstate = DPhySymbol::STATE_HS0;
				else if(v < 0.025)
					nextstate = DPhySymbol::STATE_LP00;
			}

			//Otherwise, only consider other LP states
			else
			{
				if(v > 0.88)
					nextstate = DPhySymbol::STATE_LP11;
				else if(v < 0.025)
					nextstate = DPhySymbol::STATE_LP00;
			}
		}

		//Full differential decode
		else
		{
			float vp = dp->m_samples[i];
			float vn = dn->m_samples[i];
			float vd = vp - vn;

			if( (vp < 0.55) && (vn < 0.55) )
			{
				//Can only go to a HS state from another HS state or LP-00
				if( (state == DPhySymbol::STATE_HS0) ||
					(state == DPhySymbol::STATE_HS1) ||
					(state == DPhySymbol::STATE_LP00) )
				{
					if(vd < -0.05)
					{
						nextstate = DPhySymbol::STATE_HS0;
						if(i != 0)
							start += dp->m_timescale * InterpolateTime(dp, dn, i-1, -0.05);
					}
					else if(vd > 0.05)
					{
						nextstate = DPhySymbol::STATE_HS1;
						if(i != 0)
							start += dp->m_timescale * InterpolateTime(dp, dn, i-1, 0.05);
					}
				}

				//Can only go to LP-00 from another LP state
				if( (state != DPhySymbol::STATE_HS0) && (state != DPhySymbol::STATE_HS1) )
				{
					if( (vp < 0.15) && (vn < 0.15) )
						nextstate = DPhySymbol::STATE_LP00;
				}
			}
			else if( (vp < 0.55) && (vn > 0.80) )
				nextstate = DPhySymbol::STATE_LP01;
			else if( (vp > 0.80) && (vn < 0.55) )
				nextstate = DPhySymbol::STATE_LP10;
			else if( (vp > 0.80) && (vn > 0.80) )
				nextstate = DPhySymbol::STATE_LP11;
		}

		//See if the state has changed
		size_t nsize = cap->m_samples.size();
		size_t nlast = nsize-1;
		bool samestate = (nsize && cap->m_samples[nlast].m_type == nextstate);

		//Glitch filtering
		if(!samestate && nsize)
		{
			bool last_was_glitch = false;

			//If we are transitioning from LP-00 to HS-0, we need to hold in LP-00 state for Ths-prepare first.
			//Discard any glitches to HS-0 during the transition period.
			if( (state == DPhySymbol::STATE_LP00) && (nextstate == DPhySymbol::STATE_HS0) )
			{
				//For now, set the cutoff at 30 ns. Per spec it should be 40 ns + 4 UI at the TX,
				//but we're decoding combinatorially and don't know the UI yet.
				const int64_t thsprepare_cutoff = 30000000;
				if( (cap->m_durations[nlast]) < thsprepare_cutoff )
				{
					nextstate  = DPhySymbol::STATE_LP00;
					samestate = true;
				}
			}

			//Transition from HS-0 to LP-00 isn't allowed.
			//This probably means we were never in HS-0 in the first place.
			else if( (state == DPhySymbol::STATE_HS0) && (nextstate == DPhySymbol::STATE_LP00) )
				last_was_glitch = true;

			//If the previous sample was a LP state, but significantly less than Tlpx long, discard it.
			else if( (state != DPhySymbol::STATE_HS0) && (state != DPhySymbol::STATE_HS1) )
			{
				//For now, set the cutoff at 40 ns.
				//This provides some margin on the 50 ns Tlpx in the spec.
				const int64_t tlpx_cutoff = 40000000;
				if( (cap->m_durations[nlast]) < tlpx_cutoff )
					last_was_glitch = true;
			}

			if(last_was_glitch)
			{
				//Delete the glitch sample
				cap->m_durations.pop_back();
				cap->m_offsets.pop_back();
				cap->m_samples.pop_back();

				//Update sizes
				nsize = cap->m_samples.size();
				if(nsize)
				{
					nlast = nsize-1;
					last_state = cap->m_samples[nlast].m_type;
					samestate = (nsize && last_state == nextstate);

					//If changing, extend the pre-glitch sample to the start of this sample
					if(!samestate)
						cap->m_durations[nlast] = (start) - cap->m_offsets[nlast];
				}
				else
					samestate = false;
			}
		}

		//If same as existing state, extend last one
		if(samestate)
			cap->m_durations[nlast] = start + dp->m_durations[i]*dp->m_timescale - cap->m_offsets[nlast];

		//Nope, create a new sample
		else
		{
			//Extend last sample to start of this one, if needed
			nsize = cap->m_samples.size();
			nlast = nsize-1;
			if(nsize)
				cap->m_durations[nlast] = start - cap->m_offsets[nlast];

			cap->m_offsets.push_back(start);
			cap->m_durations.push_back(dur);
			cap->m_samples.push_back(nextstate);
			state = nextstate;
		}

		last_state = state;
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
