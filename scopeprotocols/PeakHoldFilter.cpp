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
#include "PeakHoldFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PeakHoldFilter::PeakHoldFilter(const string& color)
	: PeakDetectionFilter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MATH)
{
	//Set up channels
	CreateInput("din");

	//Copy input unit
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool PeakHoldFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

double PeakHoldFilter::GetVoltageRange()
{
	return m_inputs[0].m_channel->GetVoltageRange();
}

double PeakHoldFilter::GetOffset()
{
	return m_inputs[0].m_channel->GetOffset();
}

string PeakHoldFilter::GetProtocolName()
{
	return "Peak Hold";
}

bool PeakHoldFilter::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool PeakHoldFilter::NeedsConfig()
{
	return true;
}

void PeakHoldFilter::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "PeakHold(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

void PeakHoldFilter::ClearSweeps()
{
	SetData(NULL, 0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void PeakHoldFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	//Copy units
	m_xAxisUnit = m_inputs[0].m_channel->GetXAxisUnits();
	m_yAxisUnit = m_inputs[0].m_channel->GetYAxisUnits();

	auto din = GetAnalogInputWaveform(0);

	//Create waveform if we don't have one already
	size_t len = din->m_samples.size();
	auto cap = dynamic_cast<AnalogWaveform*>(GetData(0));
	bool first = false;
	if(cap == NULL)
	{
		cap = new AnalogWaveform;
		cap->Resize(len);
		SetData(cap, 0);
		first = true;
	}

	//If sample size changed, clear it out
	if(cap->m_samples.size() != len)
	{
		cap->Resize(len);
		first = true;
	}

	//Copy timestamps from the input
	cap->CopyTimestamps(din);

	//First waveform just copies the input
	if(first)
	{
		for(size_t i=0; i<len; i++)
			cap->m_samples[i] = din->m_samples[i];
	}

	//otherwise actually do peak holding
	else
	{
		for(size_t i=0; i<len; i++)
			cap->m_samples[i] = max((float)cap->m_samples[i], (float)din->m_samples[i]);
	}

	FindPeaks(cap);

	//Copy our time scales from the input
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startPicoseconds = din->m_startPicoseconds;
}
