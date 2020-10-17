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
#include "DramRefreshActivateMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DramRefreshActivateMeasurement::DramRefreshActivateMeasurement(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MEASUREMENT)
{
	//Set up channels
	CreateInput("din");

	m_yAxisUnit = Unit(Unit::UNIT_PS);

	m_midpoint = 0;
	m_range = 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DramRefreshActivateMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (dynamic_cast<DDR3Waveform*>(stream.m_channel->GetData(stream.m_stream)) != NULL ) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void DramRefreshActivateMeasurement::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "Trfc(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string DramRefreshActivateMeasurement::GetProtocolName()
{
	return "DRAM Trfc";
}

bool DramRefreshActivateMeasurement::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool DramRefreshActivateMeasurement::NeedsConfig()
{
	return false;
}

double DramRefreshActivateMeasurement::GetVoltageRange()
{
	return m_range;
}

double DramRefreshActivateMeasurement::GetOffset()
{
	return -m_midpoint;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DramRefreshActivateMeasurement::Refresh()
{
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din = dynamic_cast<DDR3Waveform*>(GetInputWaveform(0));

	//Create the output
	auto cap = new AnalogWaveform;

	//Measure delay from refreshing a bank until an activation to the same bank
	int64_t lastRef[8] = {0, 0, 0, 0, 0, 0, 0, 0};

	float fmax = -1e20;
	float fmin =  1e20;

	int64_t tlast = 0;
	size_t len = din->m_samples.size();
	for(size_t i=0; i<len; i++)
	{
		int64_t tnow = din->m_offsets[i] * din->m_timescale;

		//Discard invalid bank IDs
		auto sample = din->m_samples[i];
		if( (sample.m_bank < 0) || (sample.m_bank > 8) )
			continue;

		//If it's a refresh, update the last refresh time
		if(sample.m_stype == DDR3Symbol::TYPE_REF)
			lastRef[sample.m_bank] = din->m_offsets[i] * din->m_timescale;

		//If it's an activate, measure the latency
		else if(sample.m_stype == DDR3Symbol::TYPE_ACT)
		{
			int64_t tact = din->m_offsets[i] * din->m_timescale;

			//If the refresh command is before the start of the capture, ignore this event
			int64_t tref= lastRef[sample.m_bank];
			if(tref == 0)
				continue;

			//Valid access, measure the latency
			int64_t latency = tact - tref;
			if(fmin > latency)
				fmin = latency;
			if(fmax < latency)
				fmax = latency;

			cap->m_offsets.push_back(tlast);
			cap->m_durations.push_back(tnow - tlast);
			cap->m_samples.push_back(latency);
			tlast = tnow;

			//Purge the last-refresh timestamp so we don't report false times for the next activate
			lastRef[sample.m_bank] = 0;
		}
	}

	if(cap->m_samples.empty())
	{
		delete cap;
		SetData(NULL, 0);
		return;
	}

	m_range = fmax - fmin + 5000;
	if(m_range < 5)
		m_range = 5;
	m_midpoint = (fmax + fmin) / 2;

	SetData(cap, 0);

	//Copy start time etc from the input. Timestamps are in picoseconds.
	cap->m_timescale = 1;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startPicoseconds = din->m_startPicoseconds;
}
