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
#include "DownconvertFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DownconvertFilter::DownconvertFilter(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_RF)
{
	//Set up channels
	ClearStreams();
	CreateInput("RF");
	AddStream(Unit(Unit::UNIT_VOLTS), "I");
	AddStream(Unit(Unit::UNIT_VOLTS), "Q");

	m_freqname = "LO Frequency";
	m_parameters[m_freqname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ));
	m_parameters[m_freqname].SetFloatVal(1e9);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DownconvertFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

float DownconvertFilter::GetVoltageRange(size_t /*stream*/)
{
	return m_inputs[0].GetVoltageRange();
}

float DownconvertFilter::GetOffset(size_t /*stream*/)
{
	return m_inputs[0].GetOffset();
}

string DownconvertFilter::GetProtocolName()
{
	return "Downconvert";
}

bool DownconvertFilter::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool DownconvertFilter::NeedsConfig()
{
	return true;
}

void DownconvertFilter::SetDefaultName()
{
	char hwname[256];
	Unit hz(Unit::UNIT_HZ);
	snprintf(hwname, sizeof(hwname), "Downconvert(%s, %s)",
		GetInputDisplayName(0).c_str(),
		m_parameters[m_freqname].ToString().c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DownconvertFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		SetData(NULL, 1);
		return;
	}

	//Get the input data
	auto din = GetAnalogInputWaveform(0);

	//Calculate phase velocity
	double lo_freq = m_parameters[m_freqname].GetFloatVal();
	double sample_freq = FS_PER_SECOND / din->m_timescale;
	double lo_cycles_per_sample = lo_freq / sample_freq;
	double lo_rad_per_sample = lo_cycles_per_sample * 2 * M_PI;
	double lo_rad_per_fs = lo_rad_per_sample / din->m_timescale;
	double trigger_phase_rad = din->m_triggerPhase * lo_rad_per_fs;

	//Do the actual mixing
	auto cap_i = SetupOutputWaveform(din, 0, 0, 0);
	auto cap_q = SetupOutputWaveform(din, 1, 0, 0);

	/*if(g_hasAvx2)
		DoFilterKernelAVX2(din, cap_i, cap_q, lo_rad_per_sample, trigger_phase_rad);
	else*/
		DoFilterKernelGeneric(din, cap_i, cap_q, lo_rad_per_sample, trigger_phase_rad);
}

void DownconvertFilter::DoFilterKernelGeneric(
	AnalogWaveform* din,
	AnalogWaveform* cap_i,
	AnalogWaveform* cap_q,
	float lo_rad_per_sample,
	float trigger_phase_rad)
{
	size_t len = din->m_samples.size();

	//Initial sample
	float phase = lo_rad_per_sample * din->m_offsets[0] + trigger_phase_rad;
	float samp = din->m_samples[0];
	cap_i->m_samples[0] 	= samp * sin(phase);
	cap_q->m_samples[0] 	= samp * cos(phase);

	for(size_t i=1; i<len; i++)
	{
		auto dt = din->m_offsets[i] - din->m_offsets[i-1];
		phase += (dt * lo_rad_per_sample);
		if(phase > 1e5*M_PI)
			phase -= 1e5*M_PI;

		samp = din->m_samples[i];
		cap_i->m_samples[i] 	= samp * sin(phase);
		cap_q->m_samples[i] 	= samp * cos(phase);
	}
}

__attribute__((target("avx2")))
void DownconvertFilter::DoFilterKernelAVX2(
	AnalogWaveform* din,
	AnalogWaveform* cap_i,
	AnalogWaveform* cap_q,
	float lo_rad_per_sample,
	float trigger_phase_rad)
{
	/*
	size_t len = din->m_samples.size();

	auto pvel = _mm256_set1_pd(lo_rad_per_sample);
	auto prad = _mm256_set1_pd(trigger_phase_rad);

	size_t i=0;
	size_t len_rounded = len - (len % 8);
	float* pin = &din->m_samples[0];
	float* pout_i = &cap_i->m_samples[i];
	float* pout_q = &cap_i->m_samples[i];
	for(; i<len_rounded; i+= 8)
	{
		auto din = _mm256_load_ps(pin + i);
	}

	//Do last few samples
	for(; i<len; i++)
	{
		double phase = lo_rad_per_sample * din->m_offsets[i] + trigger_phase_rad;
		float samp = din->m_samples[i];
		cap_i->m_samples[i] 	= samp * sin(phase);
		cap_q->m_samples[i] 	= samp * cos(phase);
	}
	*/
}
