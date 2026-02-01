/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
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


	m_displayformat = "Display Format";
	m_parameters[m_displayformat] = IBM8b10bDecoder::MakeIBM8b10bDisplayFormatParameter();
}

QSGMIIDecoder::~QSGMIIDecoder()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool QSGMIIDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i == 0) && (dynamic_cast<IBM8b10bWaveform*>(stream.m_channel->GetData(0)) != nullptr) )
		return true;

	return false;
}

string QSGMIIDecoder::GetProtocolName()
{
	return "Ethernet - QSGMII";
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
	din->PrepareForCpuAccess();
	size_t len = din->size();

	//Create the captures
	//Output is time aligned with the input
	vector<IBM8b10bWaveform*> caps;
	for(size_t i=0; i<4; i++)
	{
		auto cap = SetupEmptyWaveform<IBM8b10bWaveform>(din, i);
		cap->SetDisplayFormat(m_parameters[m_displayformat].GetIntVal());
		cap->PrepareForCpuAccess();
		caps.push_back(cap);

		//We know roughly how big each output buffer should be (1/4 the input)
		//so preallocate that space to avoid excessive allocations
		cap->Reserve(len/4);
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

		//Copy sample unless it's a K28.1. if so, convert to K28.5
		auto s = din->m_samples[i];
		if(s.m_control && (s.m_data == 0x3c) )
			caps[nlane]->m_samples.push_back(IBM8b10bSymbol(true, false, false, false, 0xbc, s.m_disparity));
		else
			caps[nlane]->m_samples.push_back(din->m_samples[i]);

		//Last sample?
		if(i+4 >= len)
			caps[nlane]->m_durations.push_back(din->m_durations[i]);

		//No, use duration of this to next one
		else
			caps[nlane]->m_durations.push_back(din->m_offsets[i+4] - din->m_offsets[i]);
	}

	for(size_t i=0; i<4; i++)
		caps[i]->MarkModifiedFromCpu();
}
