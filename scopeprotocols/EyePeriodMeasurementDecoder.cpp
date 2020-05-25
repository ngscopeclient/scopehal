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

#include "scopeprotocols.h"
#include "EyePeriodMeasurementDecoder.h"
#include "EyeDecoder2.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

EyePeriodMeasurementDecoder::EyePeriodMeasurementDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MEASUREMENT)
{
	m_yAxisUnit = Unit(Unit::UNIT_PS);

	//Set up channels
	m_signalNames.push_back("Eye");
	m_channels.push_back(NULL);

	m_value = 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool EyePeriodMeasurementDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_EYE) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void EyePeriodMeasurementDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "EyePeriod(%s)", m_channels[0]->m_displayname.c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string EyePeriodMeasurementDecoder::GetProtocolName()
{
	return "Eye Period";
}

bool EyePeriodMeasurementDecoder::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool EyePeriodMeasurementDecoder::NeedsConfig()
{
	//automatic configuration
	return false;
}

double EyePeriodMeasurementDecoder::GetVoltageRange()
{
	return 10;
}

double EyePeriodMeasurementDecoder::GetOffset()
{
	return -m_value;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void EyePeriodMeasurementDecoder::Refresh()
{
	//Get the input data
	if(m_channels[0] == NULL)
		return;
	auto din = dynamic_cast<EyeWaveform*>(m_channels[0]->GetData());
	if(din == NULL)
		return;

	//Create the output
	auto cap = new AnalogWaveform;
	cap->m_offsets.push_back(0);
	cap->m_durations.push_back(2 * din->m_uiWidth);
	m_value = din->m_uiWidth;
	cap->m_samples.push_back(m_value);

	SetData(cap);

	//Copy start time etc from the input. Timestamps are in picoseconds.
	cap->m_timescale = 1;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startPicoseconds = din->m_startPicoseconds;
}
