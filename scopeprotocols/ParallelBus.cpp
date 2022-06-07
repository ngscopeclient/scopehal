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

#include "../scopehal/scopehal.h"
#include "ParallelBus.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ParallelBus::ParallelBus(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_DIGITAL_BUS, color, CAT_BUS)
{
	//Set up channels
	char tmp[32];
	for(size_t i=0; i<16; i++)
	{
		snprintf(tmp, sizeof(tmp), "din%zu", i);
		CreateInput(tmp);
	}

	m_widthname = "Width";
	m_parameters[m_widthname] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_widthname].SetIntVal(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool ParallelBus::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 16) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string ParallelBus::GetProtocolName()
{
	return "Parallel Bus";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void ParallelBus::Refresh()
{
	//Figure out how wide our input is
	int width = m_parameters[m_widthname].GetIntVal();

	//Make sure we have an input for each channel in use
	vector<DigitalWaveform*> inputs;
	for(int i=0; i<width; i++)
	{
		auto din = GetDigitalInputWaveform(i);
		if(din == NULL)
		{
			SetData(NULL, 0);
			return;
		}
		inputs.push_back(din);
	}
	if(inputs.empty())
	{
		SetData(NULL, 0);
		return;
	}

	//Figure out length of the output
	size_t len = inputs[0]->m_samples.size();
	for(int j=1; j<width; j++)
		len = min(len, inputs[j]->m_samples.size());

	//Merge all of our samples
	//TODO: handle variable sample rates etc
	auto cap = new DigitalBusWaveform;
	cap->Resize(len);
	cap->CopyTimestamps(inputs[0]);
	#pragma omp parallel for
	for(size_t i=0; i<len; i++)
	{
		for(int j=0; j<width; j++)
			cap->m_samples[i].push_back(inputs[j]->m_samples[i]);
	}
	SetData(cap, 0);

	//Copy our time scales from the input
	cap->m_timescale = inputs[0]->m_timescale;
	cap->m_startTimestamp = inputs[0]->m_startTimestamp;
	cap->m_startFemtoseconds = inputs[0]->m_startFemtoseconds;

	//Set all unused channels to NULL
	for(size_t i=width; i < 16; i++)
	{
		auto chan = m_inputs[i].m_channel;
		if(chan != NULL)
		{
			if(chan != NULL)
				chan->Release();
			m_inputs[i].m_channel = NULL;
		}
	}
}
