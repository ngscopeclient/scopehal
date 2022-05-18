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

#include "scopeprotocols.h"
#include "PhaseMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PhaseMeasurement::PhaseMeasurement(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MEASUREMENT)
	, m_freqModeName("Frequency Mode")
	, m_freqName("Center Frequency")
{
	SetYAxisUnits(Unit(Unit::UNIT_DEGREES), 0);

	//Set up channels
	CreateInput("din");

	m_parameters[m_freqName] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_HZ));
	m_parameters[m_freqName].SetIntVal(100e6);

	m_parameters[m_freqModeName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_freqModeName].AddEnumValue("Auto", MODE_AUTO);
	m_parameters[m_freqModeName].AddEnumValue("Manual", MODE_MANUAL);
	m_parameters[m_freqModeName].SetIntVal(MODE_AUTO);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool PhaseMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if(i > 0)
		return false;

	if(stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG)
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string PhaseMeasurement::GetProtocolName()
{
	return "Phase";
}

bool PhaseMeasurement::NeedsConfig()
{
	return true;
}

float PhaseMeasurement::GetVoltageRange(size_t /*stream*/)
{
	return 370;
}

float PhaseMeasurement::GetOffset(size_t /*stream*/)
{
	return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void PhaseMeasurement::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	auto din = GetAnalogInputWaveform(0);
	float vmax = GetTopVoltage(din);
	float vmin = GetBaseVoltage(din);
	float vavg = (vmax + vmin) / 2;
	vector<int64_t> edges;
	FindZeroCrossings(din, vavg, edges);
	size_t edgelen = edges.size();

	//Auto: use median of interval between pairs of zero crossings
	int64_t period = 0;
	if(m_parameters[m_freqModeName].GetIntVal() == MODE_AUTO)
	{
		if(edgelen < 2)
		{
			SetData(NULL, 0);
			return;
		}
		vector<int64_t> durations;
		for(size_t i=0; i<edgelen-2; i++)
			durations.push_back(edges[i+2] - edges[i]);
		std::sort(durations.begin(), durations.end());
		period = durations[durations.size()/2];
	}

	//Manual: use user-selected frequency
	else
		period = FS_PER_SECOND / m_parameters[m_freqName].GetIntVal();

	//Create the output
	size_t outlen = edgelen/2;
	auto cap = SetupEmptyOutputWaveform(din, 0, true);
	cap->m_timescale = 1;
	cap->m_triggerPhase = 1;
	cap->Resize(outlen);
	cap->m_densePacked = false;

	//Main measurement loop, update once per cycle at the zero crossing.
	//This isn't quite as nice as the original implementation measuring instantaneous phase within a single cycle,
	//but is MUCH more robust in the presence of amplitude noise or variation (e.g. pulse shaping as seen in PSK31)
	for(size_t i=0; i<outlen; i++)
	{
		//Calculate normalized phase of the LO
		int64_t tnow = edges[i*2];
		float theta = fmodf(tnow, period) / period;
		theta = (theta - 0.5) * 2 * M_PI;
		if(theta < -M_PI)
			theta += 2*M_PI;
		if(theta > M_PI)
			theta -= 2*M_PI;

		cap->m_offsets[i] = tnow;
		cap->m_durations[i] = 1;
		cap->m_samples[i] = 180 * theta / M_PI;

		//Resize last sample
		if(i > 0)
			cap->m_durations[i-1] = tnow - cap->m_offsets[i-1];
	}
}
