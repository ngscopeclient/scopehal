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
#include "DigitalToPAM4Filter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DigitalToPAM4Filter::DigitalToPAM4Filter(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_GENERATION)
	, m_sampleRate("Sample Rate")
	, m_edgeTime("Transition Time")
	, m_level00("Level 00")
	, m_level01("Level 01")
	, m_level10("Level 10")
	, m_level11("Level 11")
{
	CreateInput("data");
	CreateInput("clk");

	m_parameters[m_edgeTime] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));
	m_parameters[m_edgeTime].SetIntVal(10 * 1000);

	m_parameters[m_sampleRate] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLERATE));
	m_parameters[m_sampleRate].SetIntVal(100 * 1000L * 1000L * 1000L);	//100 Gsps

	m_parameters[m_level00] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_level00].SetFloatVal(-0.3);

	m_parameters[m_level01] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_level01].SetFloatVal(-0.1);

	m_parameters[m_level10] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_level10].SetFloatVal(0.1);

	m_parameters[m_level11] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_level11].SetFloatVal(0.3);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DigitalToPAM4Filter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 2) &&
		(stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) &&
		(stream.m_channel->GetWidth() == 1)
		)
	{
		return true;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string DigitalToPAM4Filter::GetProtocolName()
{
	return "Digital to PAM4";
}

void DigitalToPAM4Filter::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "DigitalToPAM4(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

bool DigitalToPAM4Filter::NeedsConfig()
{
	return true;
}

bool DigitalToPAM4Filter::IsOverlay()
{
	return false;
}

float DigitalToPAM4Filter::GetMaxLevel()
{
	float levels[4] =
	{
		m_parameters[m_level00].GetFloatVal(),
		m_parameters[m_level01].GetFloatVal(),
		m_parameters[m_level10].GetFloatVal(),
		m_parameters[m_level11].GetFloatVal()
	};

	float v = levels[0];
	for(size_t i=1; i<4; i++)
		v = max(v, levels[i]);
	return v;
}

float DigitalToPAM4Filter::GetMinLevel()
{
	float levels[4] =
	{
		m_parameters[m_level00].GetFloatVal(),
		m_parameters[m_level01].GetFloatVal(),
		m_parameters[m_level10].GetFloatVal(),
		m_parameters[m_level11].GetFloatVal()
	};

	float v = levels[0];
	for(size_t i=1; i<4; i++)
		v = min(v, levels[i]);
	return v;
}

double DigitalToPAM4Filter::GetVoltageRange()
{
	return (GetMaxLevel() - GetMinLevel()) * 1.05;
}

double DigitalToPAM4Filter::GetOffset()
{
	float vmin = GetMinLevel();
	float vmax = GetMaxLevel();
	float vavg = (vmax + vmin)/2;
	return -vavg;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DigitalToPAM4Filter::Refresh()
{
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input and sample it
	auto din = GetDigitalInputWaveform(0);
	auto clkin = GetDigitalInputWaveform(1);
	DigitalWaveform samples;
	SampleOnAnyEdges(din, clkin, samples);

	size_t rate = m_parameters[m_sampleRate].GetIntVal();
	size_t samplePeriod = FS_PER_SECOND / rate;
	size_t edgeTime = m_parameters[m_edgeTime].GetIntVal();
	size_t edgeSamples = floor(edgeTime / samplePeriod);

	float levels[4] =
	{
		m_parameters[m_level00].GetFloatVal(),
		m_parameters[m_level01].GetFloatVal(),
		m_parameters[m_level10].GetFloatVal(),
		m_parameters[m_level11].GetFloatVal()
	};

	//Configure output waveform
	auto cap = SetupEmptyOutputWaveform(din, 0);
	cap->m_timescale = samplePeriod;
	cap->m_densePacked = true;

	//Round length to integer number of complete samples
	size_t len = samples.m_samples.size();
	if(len & 1)
		len --;

	//Adjust for start time
	int64_t capstart = samples.m_offsets[0];
	cap->m_triggerPhase = capstart;

	//Figure out how long the capture is going to be
	size_t caplen = (samples.m_offsets[len-1] + samples.m_durations[len-1] - capstart) / samplePeriod;
	cap->Resize(caplen);

	//Process samples, two at a time
	float vlast = levels[0];
	size_t nsamp = 0;
	for(size_t i=0; i<len; i+=2)
	{
		//Convert start/end times to our output timebase
		size_t tstart = (samples.m_offsets[i] - capstart);
		size_t tend = (samples.m_offsets[i+1] + samples.m_durations[i+1] - capstart);
		size_t tend_rounded = tend / samplePeriod;

		//Figure out the target voltage level
		bool s1 = samples.m_samples[i];
		bool s2 = samples.m_samples[i+1];
		int code = 0;
		if(s1)
			code |= 2;
		if(s2)
			code |= 1;
		float v = levels[code];

		size_t tEdgeDone = nsamp + edgeSamples;

		//Emit samples for the edge
		float delta = v - vlast;
		for(; nsamp < tEdgeDone; nsamp ++)
		{
			//Figure out how far along we are
			float tnow = nsamp * samplePeriod;
			float tdelta = tnow - tstart;
			float frac = max(0.0f, tdelta / edgeTime);
			float vcur = vlast + delta*frac;

			cap->m_offsets[nsamp] = nsamp;
			cap->m_durations[nsamp] = 1;
			cap->m_samples[nsamp] = vcur;
		}

		//Emit samples for the rest of the UI
		for(; nsamp < tend_rounded; nsamp ++)
		{
			cap->m_offsets[nsamp] = nsamp;
			cap->m_durations[nsamp] = 1;
			cap->m_samples[nsamp] = v;
		}

		vlast = v;
	}

	if(nsamp != caplen)
		LogDebug("Length mismatch!!\n");
}
