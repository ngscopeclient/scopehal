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
#include "WaveformGenerationFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

WaveformGenerationFilter::WaveformGenerationFilter(const string& color)
	: Filter(color, CAT_GENERATION)
	, m_sampleRate("Sample Rate")
	, m_edgeTime("Transition Time")
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput("data");
	CreateInput("clk");

	m_parameters[m_edgeTime] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));
	m_parameters[m_edgeTime].SetIntVal(10 * 1000);

	m_parameters[m_sampleRate] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLERATE));
	m_parameters[m_sampleRate].SetIntVal(100 * INT64_C(1000) * INT64_C(1000) * INT64_C(1000));	//100 Gsps
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool WaveformGenerationFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 2) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
	{
		return true;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

float WaveformGenerationFilter::GetMaxLevel()
{
	auto levels = GetVoltageLevels();
	float v = levels[0];
	for(size_t i=1; i<levels.size(); i++)
		v = max(v, levels[i]);
	return v;
}

float WaveformGenerationFilter::GetMinLevel()
{
	auto levels = GetVoltageLevels();
	float v = levels[0];
	for(size_t i=1; i<levels.size(); i++)
		v = min(v, levels[i]);
	return v;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void WaveformGenerationFilter::Refresh()
{
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input and sample it
	auto din = GetInputWaveform(0);
	auto clkin = GetInputWaveform(1);
	din->PrepareForCpuAccess();
	clkin->PrepareForCpuAccess();

	SparseDigitalWaveform samples;
	samples.PrepareForCpuAccess();
	SampleOnAnyEdgesBase(din, clkin, samples);

	size_t rate = m_parameters[m_sampleRate].GetIntVal();
	if(rate == 0)
	{
		SetData(NULL, 0);
		return;
	}
	size_t samplePeriod = FS_PER_SECOND / rate;
	size_t edgeTime = m_parameters[m_edgeTime].GetIntVal();
	size_t edgeSamples = floor(edgeTime / samplePeriod);

	//Configure output waveform
	auto cap = SetupEmptySparseAnalogOutputWaveform(din, 0);
	cap->PrepareForCpuAccess();
	cap->m_timescale = samplePeriod;

	size_t bitsPerSymbol = GetBitsPerSymbol();
	auto levels = GetVoltageLevels();

	//Round length to integer number of complete samples
	size_t len = samples.m_samples.size();
	if(bitsPerSymbol > 1)
		len -= (len % bitsPerSymbol);

	//Adjust for start time
	int64_t capstart = samples.m_offsets[0];
	cap->m_triggerPhase = capstart;

	//Figure out how long the capture is going to be
	size_t caplen = (samples.m_offsets[len-1] + samples.m_durations[len-1] - capstart) / samplePeriod;
	cap->Resize(caplen);

	//Process samples, two at a time
	float vlast = levels[0];
	size_t nsamp = 0;
	for(size_t i=0; i<len; i += bitsPerSymbol)
	{
		//Convert start/end times to our output timebase
		size_t tstart = (samples.m_offsets[i] - capstart);
		size_t tend = (samples.m_offsets[i+bitsPerSymbol-1] + samples.m_durations[i+bitsPerSymbol-1] - capstart);
		size_t tend_rounded = tend / samplePeriod;

		float v = levels[GetVoltageCode(i, samples)];
		size_t tEdgeDone = nsamp + edgeSamples;

		//Emit samples for the edge
		float delta = v - vlast;
		for(; (nsamp < tEdgeDone) && (nsamp < caplen); nsamp ++)
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
		for(; (nsamp < tend_rounded) && (nsamp < caplen); nsamp ++)
		{
			cap->m_offsets[nsamp] = nsamp;
			cap->m_durations[nsamp] = 1;
			cap->m_samples[nsamp] = v;
		}

		vlast = v;
	}

	cap->MarkModifiedFromCpu();
}
