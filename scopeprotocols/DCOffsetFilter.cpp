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
#include "DCOffsetFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DCOffsetFilter::DCOffsetFilter(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MATH)
{
	//Set up channels
	CreateInput("din");

	m_offsetname = "Offset";
	m_parameters[m_offsetname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_offsetname].SetFloatVal(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DCOffsetFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

float DCOffsetFilter::GetVoltageRange(size_t /*stream*/)
{
	return m_inputs[0].GetVoltageRange();
}

float DCOffsetFilter::GetOffset(size_t /*stream*/)
{
	return m_inputs[0].GetOffset() - m_parameters[m_offsetname].GetFloatVal();
}

string DCOffsetFilter::GetProtocolName()
{
	return "DC offset";
}

void DCOffsetFilter::SetDefaultName()
{
	char hwname[256];
	float offset = m_parameters[m_offsetname].GetFloatVal();
	if(offset >= 0)
		snprintf(hwname, sizeof(hwname), "%s + %.3f", GetInputDisplayName(0).c_str(), offset);
	else
		snprintf(hwname, sizeof(hwname), "%s %.3f", GetInputDisplayName(0).c_str(), offset);
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DCOffsetFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	auto din = GetAnalogInputWaveform(0);
	size_t len = din->m_samples.size();

	float offset = m_parameters[m_offsetname].GetFloatVal();

	//Subtract all of our samples
	auto cap = SetupOutputWaveform(din, 0, 0, 0);
	float* out = (float*)__builtin_assume_aligned(&cap->m_samples[0], 16);
	float* a = (float*)__builtin_assume_aligned(&din->m_samples[0], 16);
	for(size_t i=0; i<len; i++)
		out[i] 		= a[i] + offset;
}
