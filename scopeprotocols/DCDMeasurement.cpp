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

#include "scopeprotocols.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DCDMeasurement::DCDMeasurement(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MEASUREMENT)
{
	m_yAxisUnit = Unit(Unit::UNIT_FS);

	//Set up channels
	CreateInput("DDJ");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DCDMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (dynamic_cast<DDJMeasurement*>(stream.m_channel) != NULL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void DCDMeasurement::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "DCD(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string DCDMeasurement::GetProtocolName()
{
	return "DCD";
}

bool DCDMeasurement::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool DCDMeasurement::IsScalarOutput()
{
	return true;
}

bool DCDMeasurement::NeedsConfig()
{
	return false;
}

double DCDMeasurement::GetVoltageRange()
{
	return 1;
}

double DCDMeasurement::GetOffset()
{
	return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DCDMeasurement::Refresh()
{
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din = GetAnalogInputWaveform(0);
	auto ddj = dynamic_cast<DDJMeasurement*>(GetInput(0).m_channel);
	float* table = ddj->GetDDJTable();

	//Check all of the bins and find total jitter for rising and falling edges.
	//Note that the table has LSB most recent, so 10...... is a rising edge and 01...... is a falling edge.
	//We check for zero in case the table is incomplete (this should not drag the mean down).
	int rising_count = 0;
	float rising_sum = 0;
	int falling_count = 0;
	float falling_sum = 0;
	for(int i=0; i<256; i++)
	{
		if(table[i] == 0)
			continue;

		if(i & 0x80)
		{
			rising_count ++;
			rising_sum += table[i];
		}
		else
		{
			falling_count ++;
			falling_sum += table[i];
		}
	}

	float rising_avg = rising_sum / rising_count;
	float falling_avg = falling_sum / falling_count;
	float dcd = fabs(rising_avg - falling_avg);

	auto cap = new AnalogWaveform;
	cap->m_offsets.push_back(0);
	cap->m_durations.push_back(1);
	cap->m_samples.push_back(dcd);
	cap->m_timescale = 1;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	SetData(cap, 0);
}
