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

#include "../scopehal/scopehal.h"
#include "CurrentShuntFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

CurrentShuntFilter::CurrentShuntFilter(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_POWER)
{
	m_yAxisUnit = Unit(Unit::UNIT_AMPS);

	//Set up channels
	CreateInput("din");

	m_resistanceName = "Resistance";
	m_parameters[m_resistanceName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_OHMS));
	m_parameters[m_resistanceName].SetFloatVal(1);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool CurrentShuntFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

double CurrentShuntFilter::GetVoltageRange()
{
	double rshunt = m_parameters[m_resistanceName].GetFloatVal();
	return m_inputs[0].m_channel->GetVoltageRange() / rshunt;
}

double CurrentShuntFilter::GetOffset()
{
	double rshunt = m_parameters[m_resistanceName].GetFloatVal();
	return m_inputs[0].m_channel->GetOffset() / rshunt;
}

string CurrentShuntFilter::GetProtocolName()
{
	return "Current Shunt";
}

bool CurrentShuntFilter::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool CurrentShuntFilter::NeedsConfig()
{
	//need to specify shunt value
	return true;
}

void CurrentShuntFilter::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "Shunt(%s, %s)",
		GetInputDisplayName(0).c_str(),
		m_parameters[m_resistanceName].ToString().c_str());

	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void CurrentShuntFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	auto din = GetAnalogInputWaveform(0);
	auto len = din->m_samples.size() ;

	//Set up the output waveform
	auto cap = new AnalogWaveform;
	cap->Resize(len);
	cap->CopyTimestamps(din);

	float rshunt = m_parameters[m_resistanceName].GetFloatVal();

	float* fsrc = (float*)__builtin_assume_aligned(&din->m_samples[0], 16);
	float* fdst = (float*)__builtin_assume_aligned(&cap->m_samples[0], 16);
	float ishunt = 1.0f / rshunt;
	for(size_t i=0; i<len; i++)
		fdst[i] = fsrc[i] * ishunt;

	//Copy our time scales from the input
	cap->m_timescale 		= din->m_timescale;
	cap->m_startTimestamp 	= din->m_startTimestamp;
	cap->m_startPicoseconds = din->m_startPicoseconds;

	SetData(cap, 0);
}
