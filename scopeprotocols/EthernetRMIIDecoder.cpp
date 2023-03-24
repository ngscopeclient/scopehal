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
#include "EthernetProtocolDecoder.h"
#include "EthernetRMIIDecoder.h"
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

EthernetRMIIDecoder::EthernetRMIIDecoder(const string& color)
	: EthernetProtocolDecoder(color)
{
	//Digital inputs, so need to undo some stuff for the PHY layer decodes
	m_signalNames.clear();
	m_inputs.clear();

	//Add inputs. Make data be the first, because we normally want the overlay shown there.
	CreateInput("clk");
	CreateInput("ctl");
	CreateInput("d0");
	CreateInput("d1");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string EthernetRMIIDecoder::GetProtocolName()
{
	return "Ethernet - RMII";
}

bool EthernetRMIIDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	auto chan = stream.m_channel;
	if(chan == NULL)
		return false;

	if(stream.GetType() != Stream::STREAM_TYPE_DIGITAL)
		return false;

	if(i < 4)
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void EthernetRMIIDecoder::Refresh()
{
	ClearPackets();

	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto clk = GetInputWaveform(0);
	auto ctl = GetInputWaveform(1);
	auto d0 = GetInputWaveform(2);
	auto d1 = GetInputWaveform(3);

	//Sample everything on the clock edges
	SparseDigitalWaveform dctl;
	SparseDigitalWaveform dd0;
	SparseDigitalWaveform dd1;
	SampleOnRisingEdgesBase(ctl, clk, dctl);
	SampleOnRisingEdgesBase(d0, clk, dd0);
	SampleOnRisingEdgesBase(d1, clk, dd1);

	//Need a reasonable number of samples or there's no point in decoding.
	size_t len = min(dctl.size(), dd0.size());
	len = min(len, dd1.size());
	if(len < 100)
	{
		SetData(NULL, 0);
		return;
	}
	len -= 4;	//we read past current position to get a full byte

	//Create the output capture
	auto cap = new EthernetWaveform;
	cap->m_timescale = 1;
	cap->m_startTimestamp = clk->m_startTimestamp;
	cap->m_startFemtoseconds = clk->m_startFemtoseconds;
	cap->PrepareForCpuAccess();

	//skip first 2 samples so we can get a full clock cycle before starting
	for(size_t i=2; i < len; i++)
	{
		//If ctl is 0, nothing happening
		if(!dctl.m_samples[i])
			continue;

		//No preamble (ctl high, d0 high, d1 low) yet
		if(!dd0.m_samples[i])
			continue;

		//Set of recovered bytes and timestamps
		vector<uint8_t> bytes;
		vector<uint64_t> starts;
		vector<uint64_t> ends;

		//TODO: handle error signal (ignored for now)
		bool err = false;
		while( (i < len) && (dctl.m_samples[i]) )
		{
			//Timestamps
			starts.push_back(dd0.m_offsets[i]);
			ends.push_back(dd0.m_offsets[i+3] + dd0.m_durations[i+3]);

			//Convert di-bits to bytes
			//We send LSB first, MSB last
			uint8_t dval = 0;
			for(size_t j=0; j<4; j ++)
			{
				if(dd0.m_samples[i+j])
					dval |= (1 << j*2);
				if(dd1.m_samples[i+j])
					dval |= (2 << j*2);

				if(!dctl.m_samples[i+j])
				{
					LogDebug("ctl ended partway through a byte at i=%zu, j=%zu)\n", i, j);
					err = true;
					break;
				}
			}
			bytes.push_back(dval);
			i += 4;
			if(err)
				break;
		}

		//Crunch the data
		BytesToFrames(bytes, starts, ends, cap);
	}

	SetData(cap, 0);
	cap->MarkModifiedFromCpu();
}
