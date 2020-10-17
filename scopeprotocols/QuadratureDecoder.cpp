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
#include "QuadratureDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

QuadratureDecoder::QuadratureDecoder(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MISC)
{
	//Set up channels
	CreateInput("A");
	CreateInput("B");
	//CreateInput("Reset");

	m_pulseratename = "Pulses per rev";
	m_parameters[m_pulseratename] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_pulseratename].SetFloatVal(0);

	m_interpname = "Interpolation";
	m_parameters[m_interpname] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_interpname].AddEnumValue("None", INTERP_NONE);
	m_parameters[m_interpname].AddEnumValue("Linear", INTERP_LINEAR);

	m_revname = "Revolutions";
	m_parameters[m_revname] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_revname].AddEnumValue("Single", MODE_SINGLE_REV);
	m_parameters[m_revname].AddEnumValue("Multi", MODE_MULTI_REV);

	m_debouncename = "Debounce Cooldown";
	m_parameters[m_debouncename] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_PS));
	m_parameters[m_debouncename].ParseString("1 ms");

	m_yAxisUnit = Unit(Unit::UNIT_DEGREES);

	m_max = 10;
	m_min = -10;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool QuadratureDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 2) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void QuadratureDecoder::ClearSweeps()
{
	m_max = 10;
	m_min = -10;
	SetData(NULL, 0);
}

double QuadratureDecoder::GetVoltageRange()
{
	return (m_max - m_min) + 20;
}

double QuadratureDecoder::GetOffset()
{
	return -( (m_max - m_min)/2 + m_min );
}

string QuadratureDecoder::GetProtocolName()
{
	return "Quadrature";
}

bool QuadratureDecoder::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool QuadratureDecoder::NeedsConfig()
{
	return true;
}

void QuadratureDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(
		hwname,
		sizeof(hwname),
		"Quadrature(%s,%s)",
		GetInputDisplayName(0).c_str(),
		GetInputDisplayName(1).c_str()
		);

	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void QuadratureDecoder::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	auto a = GetDigitalInputWaveform(0);
	auto b = GetDigitalInputWaveform(1);

	float phase_per_pulse = 360 / m_parameters[m_pulseratename].GetFloatVal();

	InterpolationMode mode = (InterpolationMode)m_parameters[m_interpname].GetIntVal();
	RevMode rmode = (RevMode)m_parameters[m_revname].GetIntVal();
	int64_t debounce_ps = m_parameters[m_debouncename].GetIntVal();
	int64_t debounce_samples = debounce_ps / a->m_timescale;

	//Create the output waveform
	auto cap = new AnalogWaveform;
	cap->m_timescale = a->m_timescale;
	cap->m_startTimestamp = a->m_startTimestamp;
	cap->m_startPicoseconds = a->m_startPicoseconds;

	//Seed with initial point at time zero
	int64_t last_edge = 0;
	cap->m_offsets.push_back(0);
	cap->m_durations.push_back(1);
	cap->m_samples.push_back(0);

	int64_t timestamp = 0;
	size_t ia = 0;
	size_t ib = 0;
	size_t alen = a->m_offsets.size();
	size_t blen = b->m_offsets.size();

	bool last_a = a->m_samples[0];
	bool last_b = b->m_samples[0];
	float phase = 0;

	enum
	{
		STATE_BOTH_HIGH,
		STATE_A_HIGH,
		STATE_BOTH_LOW,
		STATE_B_HIGH
	} state = STATE_BOTH_LOW;

	if(last_a && last_b)
		state = STATE_BOTH_HIGH;

	while(true)
	{
		bool ca = a->m_samples[ia];
		bool cb = b->m_samples[ib];

		//TODO: add mode to say look for both edges or only rising

		//Lagging phase
		size_t ilast = cap->m_durations.size() - 1;
		bool edge = false;
		bool phase_positive = true;

		//Ignore toggles for a user-specified time after another toggle
		int64_t samples_since_edge = (timestamp - last_edge);
		if( samples_since_edge >= debounce_samples)
		{
			switch(state)
			{
				//Both signals are low. Look for a rising edge
				case STATE_BOTH_LOW:

					//A is leading
					if(ca)
					{
						phase_positive = false;
						edge = true;
						state = STATE_A_HIGH;
					}

					//A is lagging
					if(cb)
					{
						phase_positive = true;
						edge = true;
						state = STATE_B_HIGH;
					}

					break;

				//Ignore edges until the other signal toggles
				case STATE_A_HIGH:
					if(cb)
						state = STATE_BOTH_HIGH;
					if(!ca)
						state = STATE_BOTH_LOW;
					break;
				case STATE_B_HIGH:
					if(ca)
						state = STATE_BOTH_HIGH;
					if(!cb)
						state = STATE_BOTH_LOW;
					break;

				//Both are high, look for falling edges
				case STATE_BOTH_HIGH:

					//A is leading
					if(!ca)
					{
						phase_positive = false;
						//edge = true;
						state = STATE_B_HIGH;
					}

					//A is lagging
					if(!cb)
					{
						phase_positive = true;
						//edge = true;
						state = STATE_A_HIGH;
					}

					break;
			}
		}

		//Add samples if we get a pulse
		if(edge)
		{
			last_edge = timestamp;

			if(phase_positive)
			{
				phase += phase_per_pulse;
				if(rmode == MODE_SINGLE_REV)
				{
					if(phase > 180)
						phase -= 360;
				}
			}
			else
			{
				phase -= phase_per_pulse;
				if(rmode == MODE_MULTI_REV)
				{
					if(phase < -180)
						phase += 360;
				}
			}

			if(mode == INTERP_LINEAR)
			{
				//Update duration of last pulse
				cap->m_durations[ilast] = timestamp - last_edge;

				//Add the new sample
				cap->m_offsets.push_back(timestamp);
				cap->m_durations.push_back(0);
				cap->m_samples.push_back(phase);
			}
			else
			{
				//Update duration of last pulse
				cap->m_durations[ilast] = timestamp - last_edge - 1;

				//Add a new sample for the edge
				cap->m_offsets.push_back(timestamp-1);
				cap->m_durations.push_back(1);
				cap->m_samples.push_back(cap->m_samples[ilast]);

				//Add a new sample for the point
				cap->m_offsets.push_back(timestamp);
				cap->m_durations.push_back(0);
				cap->m_samples.push_back(phase);
			}
		}

		//Store phase limits
		m_min = min(m_min, phase);
		m_max = max(m_max, phase);

		//Save current values for edge detection
		last_a = ca;
		last_b = cb;

		//Get timestamps of next event on each channel.
		//If we can't move forward, stop.
		int64_t next_a = GetNextEventTimestamp(a, ia, alen, timestamp);
		int64_t next_b = GetNextEventTimestamp(b, ib, blen, timestamp);
		int64_t next_timestamp = min(next_a, next_b);
		if(next_timestamp == timestamp)
			break;
		timestamp = next_timestamp;
		AdvanceToTimestamp(a, ia, alen, timestamp);
		AdvanceToTimestamp(b, ib, blen, timestamp);
	}

	//If less than 2 samples, stop
	if(cap->m_durations.size() < 2)
	{
		delete cap;
		SetData(NULL, 0);
		return;
	}

	//Extend last sample
	size_t ilast = cap->m_durations.size() - 1;
	cap->m_durations[ilast] = timestamp - last_edge;

	SetData(cap, 0);
}
