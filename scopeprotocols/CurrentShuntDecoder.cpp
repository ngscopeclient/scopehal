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
#include "CurrentShuntDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

CurrentShuntDecoder::CurrentShuntDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_POWER)
{
	m_yAxisUnit = Unit(Unit::UNIT_AMPS);

	//Set up channels
	m_signalNames.push_back("din");
	m_channels.push_back(NULL);

	m_resistanceName = "Resistance";
	m_parameters[m_resistanceName] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_FLOAT);
	m_parameters[m_resistanceName].SetFloatVal(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool CurrentShuntDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

double CurrentShuntDecoder::GetVoltageRange()
{
	double rshunt = m_parameters[m_resistanceName].GetFloatVal();
	return m_channels[0]->GetVoltageRange() / rshunt;
}

double CurrentShuntDecoder::GetOffset()
{
	double rshunt = m_parameters[m_resistanceName].GetFloatVal();
	return m_channels[0]->GetOffset() / rshunt;
}

string CurrentShuntDecoder::GetProtocolName()
{
	return "Current Shunt";
}

bool CurrentShuntDecoder::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool CurrentShuntDecoder::NeedsConfig()
{
	//need to specify shunt value
	return true;
}

void CurrentShuntDecoder::SetDefaultName()
{
	Unit uohms(Unit::UNIT_OHMS);

	char hwname[256];
	snprintf(hwname, sizeof(hwname), "Shunt(%s, %s)",
		m_channels[0]->m_displayname.c_str(),
		uohms.PrettyPrint(m_parameters[m_resistanceName].GetFloatVal()).c_str());

	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void CurrentShuntDecoder::Refresh()
{
	//Get the input data
	if(m_channels[0] == NULL)
	{
		SetData(NULL);
		return;
	}
	auto din = dynamic_cast<AnalogWaveform*>(m_channels[0]->GetData());
	if(!din)
	{
		SetData(NULL);
		return;
	}

	//We need meaningful data
	auto len = din->m_samples.size() ;
	if(len == 0)
	{
		SetData(NULL);
		return;
	}

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

	SetData(cap);
}
