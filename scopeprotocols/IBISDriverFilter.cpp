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
#include "IBISDriverFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

IBISDriverFilter::IBISDriverFilter(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_GENERATION)
	, m_sampleRate("Sample Rate")
{
	CreateInput("data");
	CreateInput("clk");

	m_parameters[m_sampleRate] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLERATE));
	m_parameters[m_sampleRate].SetIntVal(100 * 1000L * 1000L * 1000L);	//100 Gsps

	m_parser.Load("/ceph/bulk/datasheets/Xilinx/7_series/kintex-7/kintex7.ibs");
	m_model = m_parser.m_models["SSTL135_F_HR"];

	ClearSweeps();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool IBISDriverFilter::ValidateChannel(size_t i, StreamDescriptor stream)
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

string IBISDriverFilter::GetProtocolName()
{
	return "IBIS Driver";
}

void IBISDriverFilter::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "IBIS(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

float IBISDriverFilter::GetVoltageRange(size_t /*stream*/)
{
	return m_range;
}

float IBISDriverFilter::GetOffset(size_t /*stream*/)
{
	return m_offset;
}

bool IBISDriverFilter::NeedsConfig()
{
	return true;
}

bool IBISDriverFilter::IsOverlay()
{
	return false;
}

void IBISDriverFilter::ClearSweeps()
{
	m_vmax = FLT_MIN;
	m_vmin = FLT_MAX;
	m_range = 1;
	m_offset = 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void IBISDriverFilter::Refresh()
{
	if(!VerifyAllInputsOK() || !m_model)
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

	//Configure output waveform
	auto cap = SetupEmptyOutputWaveform(din, 0);
	cap->m_timescale = samplePeriod;
	cap->m_densePacked = true;

	//Round length to integer number of complete cycles
	size_t len = samples.m_samples.size();

	//Adjust for start time
	int64_t capstart = samples.m_offsets[0];
	cap->m_triggerPhase = capstart;

	//Figure out how long the capture is going to be
	size_t caplen = (samples.m_offsets[len-1] + samples.m_durations[len-1] - capstart) / samplePeriod;
	cap->Resize(caplen);

	//Find the rising and falling edge waveform terminated to the highest voltage (Vcc etc)
	//TODO: make this configurable
	VTCurves* rising = m_model->GetHighestRisingWaveform();
	VTCurves* falling = m_model->GetHighestFallingWaveform();
	IBISCorner corner = CORNER_TYP;

	//Process samples
	size_t nsamp = 0;
	bool last = samples.m_samples[0];
	for(size_t i=0; i<len; i ++)
	{
		//Get start/end times of this UI
		size_t tstart = samples.m_offsets[i] - capstart;
		size_t tend = tstart + samples.m_durations[i];
		size_t tend_rounded = tend / samplePeriod;
		size_t nstart = nsamp;

		//If this UI is NOT a toggle, just echo a constant value for every sample
		auto cur = samples.m_samples[i];
		if(cur == last)
		{
			float v;
			if(cur)
				v = falling->InterpolateVoltage(corner, 0);
			else
				v = rising->InterpolateVoltage(corner, 0);

			for(; (nsamp < tend_rounded) && (nsamp < caplen); nsamp ++)
			{
				cap->m_offsets[nsamp] = nsamp;
				cap->m_durations[nsamp] = 1;
				cap->m_samples[nsamp] = v;
			}

			m_vmax = max(m_vmax, v);
			m_vmin = min(m_vmin, v);
		}

		//Toggle, play back the waveform
		else
		{
			for(; (nsamp < tend_rounded) && (nsamp < caplen); nsamp ++)
			{
				cap->m_offsets[nsamp] = nsamp;
				cap->m_durations[nsamp] = 1;

				//IBIS timestamps are in seconds not fs
				float toff = (nsamp - nstart) * samplePeriod / FS_PER_SECOND;
				float v;
				if(cur)
					v = rising->InterpolateVoltage(corner, toff);
				else
					v = falling->InterpolateVoltage(corner, toff);
				cap->m_samples[nsamp] = v;

				m_vmax = max(m_vmax, v);
				m_vmin = min(m_vmin, v);
			}
		}

		last = cur;
	}

	m_range = (m_vmax - m_vmin) * 1.05;
	m_offset = -( (m_vmax - m_vmin)/2 + m_vmin );
}
