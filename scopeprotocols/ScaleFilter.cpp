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
#include "ScaleFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ScaleFilter::ScaleFilter(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MATH)
{
	//Set up channels
	CreateInput("din");

	m_scalefactorname = "Scale Factor";
	m_parameters[m_scalefactorname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_scalefactorname].SetFloatVal(1);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool ScaleFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

float ScaleFilter::GetVoltageRange(size_t /*stream*/)
{
    // FIXME: This is akward and couples the scaling of the filtered waveform to the scaling
    // of the input waveform, i.e. the only way to adjust scaling on the output is via
    // adjusting the scaling of the input
	return m_inputs[0].GetVoltageRange() * m_parameters[m_scalefactorname].GetFloatVal();
}

float ScaleFilter::GetOffset(size_t /*stream*/)
{
	return m_inputs[0].GetOffset();// * m_parameters[m_scalefactorname].GetFloatVal();
}

string ScaleFilter::GetProtocolName()
{
	return "Scale";
}

bool ScaleFilter::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool ScaleFilter::NeedsConfig()
{
	//we need the offset to be specified, duh
	return true;
}

void ScaleFilter::SetDefaultName()
{
	char hwname[256];
	float scalefactor = m_parameters[m_scalefactorname].GetFloatVal();
	snprintf(hwname, sizeof(hwname), "%s * %.3f", GetInputDisplayName(0).c_str(), scalefactor);
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void ScaleFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	auto din = GetAnalogInputWaveform(0);
	size_t len = din->m_samples.size();

	float scalefactor = m_parameters[m_scalefactorname].GetFloatVal();

	//Multiply all of our samples by the scale factor
	auto cap = SetupOutputWaveform(din, 0, 0, 0);
	float* out = (float*)__builtin_assume_aligned(&cap->m_samples[0], 16);
	float* a = (float*)__builtin_assume_aligned(&din->m_samples[0], 16);
	for(size_t i=0; i<len; i++)
		out[i] = a[i] * scalefactor;
}
