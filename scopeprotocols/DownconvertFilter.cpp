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
	size_t len = din->m_samples.size();

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
	for(size_t i=0; i<len; i++)
	{
		//TODO: Figure out how to accumulate this in single precision so we can get more throughput
		double phase = lo_rad_per_sample * din->m_offsets[i] + trigger_phase_rad;

		float samp = din->m_samples[i];
		cap_i->m_samples[i] 	= samp * sin(phase);
		cap_q->m_samples[i] 	= samp * cos(phase);
	}

	//Copy our time scales from the input
	cap_i->m_timescale 			= din->m_timescale;
	cap_q->m_timescale 			= din->m_timescale;
	cap_i->m_startTimestamp 	= din->m_startTimestamp;
	cap_q->m_startTimestamp 	= din->m_startTimestamp;
	cap_i->m_startFemtoseconds	= din->m_startFemtoseconds;
	cap_q->m_startFemtoseconds	= din->m_startFemtoseconds;
}
