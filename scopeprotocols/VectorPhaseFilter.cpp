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

#include "scopeprotocols.h"
#include "VectorPhaseFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

VectorPhaseFilter::VectorPhaseFilter(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_RF)
{
	//Set up channels
	CreateInput("I");
	CreateInput("Q");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool VectorPhaseFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if(i > 1)
		return false;

	if(stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG)
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string VectorPhaseFilter::GetProtocolName()
{
	return "Vector Phase";
}

float VectorPhaseFilter::GetVoltageRange(size_t /*stream*/)
{
	return 370;
}

float VectorPhaseFilter::GetOffset(size_t /*stream*/)
{
	return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void VectorPhaseFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto a = GetAnalogInputWaveform(0);
	auto b = GetAnalogInputWaveform(1);
	auto len = min(a->m_samples.size(), b->m_samples.size());

	//Set up the output waveform
	auto cap = new AnalogWaveform;
	cap->Resize(len);
	cap->CopyTimestamps(a);

	float* fa = (float*)&a->m_samples[0];
	float* fb = (float*)&b->m_samples[0];
	float scale = 180 / M_PI;
	for(size_t i=0; i<len; i++)
		cap->m_samples[i] = atan2(fa[i], fb[i]) * scale;

	//Copy our time scales from the input
	cap->m_timescale 		= a->m_timescale;
	cap->m_startTimestamp 	= a->m_startTimestamp;
	cap->m_startFemtoseconds = a->m_startFemtoseconds;

	SetYAxisUnits(Unit(Unit::UNIT_DEGREES), 0);

	SetData(cap, 0);
}
