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
#include "PhaseMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PhaseMeasurement::PhaseMeasurement(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MEASUREMENT)
{
	m_yAxisUnit = Unit(Unit::UNIT_DEGREES);

	//Set up channels
	CreateInput("din");
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

void PhaseMeasurement::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "Phase(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string PhaseMeasurement::GetProtocolName()
{
	return "Phase";
}

bool PhaseMeasurement::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool PhaseMeasurement::NeedsConfig()
{
	//automatic configuration
	return false;
}

double PhaseMeasurement::GetVoltageRange()
{
	return 370;
}

double PhaseMeasurement::GetOffset()
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

	//Get the median period of the input signal to run our LO at
	auto din = GetAnalogInputWaveform(0);
	float vmax = GetTopVoltage(din);
	float vmin = GetBaseVoltage(din);
	float vavg = (vmax + vmin) / 2;
	float vpp2 = (vmax - vmin) / 2;
	vector<int64_t> edges;
	FindZeroCrossings(din, vavg, edges);
	if(edges.size() < 2)
	{
		SetData(NULL, 0);
		return;
	}
	vector<int64_t> durations;
	for(size_t i=0; i<edges.size()-2; i++)
		durations.push_back(edges[i+2] - edges[i]);
	std::sort(durations.begin(), durations.end());
	int64_t period = durations[durations.size()/2];

	//Create the output
	auto cap = SetupOutputWaveform(din, 0, 0, 0);
	size_t len = din->m_samples.size();
	for(size_t i=0; i<len; i++)
	{
		//Calculate normalized phase of the LO
		int64_t tnow = din->m_offsets[i] * din->m_timescale + din->m_triggerPhase;
		float lophase = fmodf(tnow, period) / period;

		//Normalize signal amplitude to an ideal +/- 1V sinusoid
		float vin = din->m_samples[i];
		float vnorm = (vin - vavg) / vpp2;
		vnorm = max(vnorm, -1.0f);
		vnorm = min(vnorm, 1.0f);

		//Calculate input signal phase. asinf returns -pi/2 to +pi/2, we need to disambiguate outside that range.
		//To do that, figure out which half of the sine we're in: if the derivative of the input is positive,
		//we're in the zero centered half of the wave. If negative, we're in the other half.
		float theta = asinf(vnorm);
		float delta;
		if(i == 0)
			delta = din->m_samples[1] - din->m_samples[0];
		else
			delta = vin - din->m_samples[i-1];

		if(delta < 0)
		{
			if(theta < 0)
				theta = -M_PI - theta;
			else
				theta = M_PI - theta;
		}

		//Convert LO phase from 0-1 to +/- pi
		lophase = (lophase - 0.5) * 2 * M_PI;

		//Convert absolute to relative phase and normalize.
		theta -= lophase;
		if(theta < -M_PI)
			theta += 2*M_PI;
		if(theta > M_PI)
			theta -= 2*M_PI;

		//Convert radians to degrees for final output
		cap->m_samples[i] = 180 * theta / M_PI;
	}
}
