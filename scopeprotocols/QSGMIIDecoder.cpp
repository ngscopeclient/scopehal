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
	@brief Implementation of QSGMIIDecoder
 */
#include "../scopehal/scopehal.h"
#include "../scopehal/Filter.h"
#include "IBM8b10bDecoder.h"
#include "QSGMIIDecoder.h"


using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

QSGMIIDecoder::QSGMIIDecoder(const string& color)
	: Filter(color, CAT_SERIAL)
{
	CreateInput("data");

	AddProtocolStream("Lane 0");
	AddProtocolStream("Lane 1");
	AddProtocolStream("Lane 2");
	AddProtocolStream("Lane 3");
}

QSGMIIDecoder::~QSGMIIDecoder()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool QSGMIIDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (dynamic_cast<IBM8b10bWaveform*>(stream.m_channel->GetData(0)) != NULL) )
		return true;

	return false;
}

string QSGMIIDecoder::GetProtocolName()
{
	return "QSGMII";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void QSGMIIDecoder::Refresh()
{
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input waveform
	auto din = dynamic_cast<IBM8b10bWaveform*>(GetInputWaveform(0));
	size_t len = din->m_offsets.size();

	//Create the captures
	//Output is time aligned with the input
	vector<IBM8b10bWaveform*> caps;
	for(size_t i=0; i<4; i++)
	{
		auto cap = new IBM8b10bWaveform;

		cap->m_timescale = 1;
		cap->m_startTimestamp = din->m_startTimestamp;
		cap->m_startFemtoseconds = din->m_startFemtoseconds;
		cap->m_triggerPhase = 0;
		cap->m_densePacked = false;

		caps.push_back(cap);

		SetData(cap, i);
	}

	//Find the first K28.1 (control 0x3c)
	size_t phase = 0;
	bool found = false;
	for(size_t i=0; i<len; i++)
	{
		auto s = din->m_samples[i];
		if(s.m_control && (s.m_data == 0x3c) )
		{
			phase = i & 3;
			found = true;
			break;
		}
	}

	//If no K28.1, give up
	if(!found)
		return;

	//Go through the list of symbols and round-robin them out to each lane
	for(size_t i=0; i<len; i++)
	{
		size_t nlane = (i - phase) & 3;

		caps[nlane]->m_offsets.push_back(din->m_offsets[i]);
		caps[nlane]->m_samples.push_back(din->m_samples[i]);

		//Last sample?
		if(i+4 >= len)
			caps[nlane]->m_durations.push_back(din->m_durations[i]);

		//No, use duration of this to next one
		else
			caps[nlane]->m_durations.push_back(din->m_offsets[i+4] - din->m_offsets[i]);
	}
}

Gdk::Color QSGMIIDecoder::GetColor(int i)
{
	auto capture = dynamic_cast<IBM8b10bWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const IBM8b10bSymbol& s = capture->m_samples[i];

		if(s.m_error)
			return m_standardColors[COLOR_ERROR];
		else if(s.m_control)
			return m_standardColors[COLOR_CONTROL];
		else
			return m_standardColors[COLOR_DATA];
	}

	//error
	return m_standardColors[COLOR_ERROR];
}

//TODO: this is pulled directly from the 8B10B decode, can we figure out how to refactor so this is cleaner?
string QSGMIIDecoder::GetText(int i)
{
	auto capture = dynamic_cast<IBM8b10bWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const IBM8b10bSymbol& s = capture->m_samples[i];

		unsigned int right = s.m_data >> 5;
		unsigned int left = s.m_data & 0x1F;

		char tmp[32];
		if(s.m_error)
			return "ERROR";
		else
		{
			//Dotted format
			//if(m_cachedDisplayFormat == FORMAT_DOTTED)
			if(true)
			{
				if(s.m_control)
					snprintf(tmp, sizeof(tmp), "K%u.%u", left, right);
				else
					snprintf(tmp, sizeof(tmp), "D%u.%u", left, right);

				if(s.m_disparity < 0)
					return string(tmp) + "-";
				else
					return string(tmp) + "+";
			}

			//Hex format
			else
			{
				if(s.m_control)
					snprintf(tmp, sizeof(tmp), "K.%02x", s.m_data);
				else
					snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
				return string(tmp);
			}
		}
	}
	return "";
}

