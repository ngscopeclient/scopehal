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
#include "SquelchFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SquelchFilter::SquelchFilter(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_RF)
{
	//Set up channels
	CreateInput("I");
	CreateInput("Q");
	ClearStreams();
	AddStream(Unit(Unit::UNIT_VOLTS), "I");
	AddStream(Unit(Unit::UNIT_VOLTS), "Q");

	m_thresholdname = "Threshold";
	m_parameters[m_thresholdname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_thresholdname].SetFloatVal(0.01);

	m_holdtimename = "Hold time";
	m_parameters[m_holdtimename] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));
	m_parameters[m_holdtimename].SetIntVal(1e6);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool SquelchFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 2) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

float SquelchFilter::GetVoltageRange(size_t stream)
{
	if(stream == 1)
		return m_inputs[1].GetVoltageRange();
	else
		return m_inputs[0].GetVoltageRange();
}

float SquelchFilter::GetOffset(size_t stream)
{
	if(stream == 1)
		return m_inputs[1].GetOffset();
	else
		return m_inputs[0].GetOffset();
}

string SquelchFilter::GetProtocolName()
{
	return "Squelch";
}

bool SquelchFilter::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool SquelchFilter::NeedsConfig()
{
	return true;
}

void SquelchFilter::SetDefaultName()
{
	char hwname[256];
	snprintf(
		hwname, sizeof(hwname), "Squelch(%s, %s)", GetInputDisplayName(0).c_str(), GetInputDisplayName(1).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void SquelchFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	auto din_i = GetAnalogInputWaveform(0);
	auto din_q = GetAnalogInputWaveform(1);
	size_t len = min(din_i->m_samples.size(), din_q->m_samples.size());

	auto threshold = m_parameters[m_thresholdname].GetFloatVal();
	auto holdtime_fs = m_parameters[m_holdtimename].GetIntVal();
	auto holdtime_samples = holdtime_fs / din_i->m_timescale;

	auto dout_i = SetupOutputWaveform(din_i, 0, 0, 0);
	auto dout_q = SetupOutputWaveform(din_q, 1, 0, 0);

	bool open = false;
	float tsq = threshold * threshold;
	size_t topen = 0;
	for(size_t i=0; i<len; i++)
	{
		//Find I/Q magnitude (do comparison on squared mag to avoid a ton of sqrts)
		float vi = din_i->m_samples[i];
		float vq = din_q->m_samples[i];
		float msq = vi*vi + vq*vq;

		//Signal amplitude is above threshold - open squelch immediately
		//TODO: attack time?
		if(msq > tsq)
		{
			open = true;
			topen = i;
		}

		//Signal amplitude below threshold - close squelch after hold time elapses
		else if(open && ( (i - topen) > holdtime_samples) )
			open = false;

		if(open)
		{
			dout_i->m_samples[i] = din_i->m_samples[i];
			dout_q->m_samples[i] = din_q->m_samples[i];
		}
		else
		{
			dout_i->m_samples[i] = 0.0f;
			dout_q->m_samples[i] = 0.0f;
		}
	}

}
