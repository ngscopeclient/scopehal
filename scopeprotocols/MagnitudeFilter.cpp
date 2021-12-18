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
#include "MagnitudeFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

MagnitudeFilter::MagnitudeFilter(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_RF)
{
	//Set up channels
	CreateInput("I");
	CreateInput("Q");

	m_range = 1;
	m_offset = 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool MagnitudeFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 2) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

double MagnitudeFilter::GetVoltageRange()
{
	return m_range;
}

double MagnitudeFilter::GetOffset()
{
	return -m_offset;
}

string MagnitudeFilter::GetProtocolName()
{
	return "Vector Magnitude";
}

bool MagnitudeFilter::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool MagnitudeFilter::NeedsConfig()
{
	return true;
}

void MagnitudeFilter::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "Magnitude(%s, %s)",
		GetInputDisplayName(0).c_str(),
		GetInputDisplayName(1).c_str());

	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void MagnitudeFilter::Refresh()
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

	//Copy Y axis units from input
	SetYAxisUnits(m_inputs[0].GetYAxisUnits(), 0);

	//Set up the output waveform
	auto cap = new AnalogWaveform;
	cap->Resize(len);
	cap->CopyTimestamps(a);

	float* fa = (float*)__builtin_assume_aligned(&a->m_samples[0], 16);
	float* fb = (float*)__builtin_assume_aligned(&b->m_samples[0], 16);
	float* fdst = (float*)__builtin_assume_aligned(&cap->m_samples[0], 16);
	for(size_t i=0; i<len; i++)
		fdst[i] = sqrtf(fa[i]*fa[i] + fb[i]*fb[i]);

	//Calculate range of the output waveform
	float x = GetMaxVoltage(cap);
	float n = GetMinVoltage(cap);
	m_range = x - n;
	m_offset = (x+n)/2;

	//Copy our time scales from the input
	cap->m_timescale 		= a->m_timescale;
	cap->m_startTimestamp 	= a->m_startTimestamp;
	cap->m_startFemtoseconds = a->m_startFemtoseconds;

	SetData(cap, 0);
}
