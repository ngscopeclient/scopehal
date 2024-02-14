/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2024 Andrew D. Zonenberg and contributors                                                         *
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
#include "IQDemuxFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

IQDemuxFilter::IQDemuxFilter(const string& color)
	: Filter(color, CAT_RF)
	, m_alignment("Alignment")
{
	AddStream(Unit(Unit::UNIT_VOLTS), "I", Stream::STREAM_TYPE_ANALOG);
	AddStream(Unit(Unit::UNIT_VOLTS), "Q", Stream::STREAM_TYPE_ANALOG);
	AddStream(Unit(Unit::UNIT_COUNTS), "clk", Stream::STREAM_TYPE_DIGITAL);

	CreateInput("din");
	CreateInput("clk");

	m_parameters[m_alignment] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_alignment].AddEnumValue("None", ALIGN_NONE);
	m_parameters[m_alignment].AddEnumValue("100Base-T1", ALIGN_100BASET1);
	m_parameters[m_alignment].SetIntVal(ALIGN_NONE);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool IQDemuxFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;
	if( (i == 1) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string IQDemuxFilter::GetProtocolName()
{
	return "IQ Demux";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void IQDemuxFilter::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	auto din = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(0));
	auto clk = dynamic_cast<SparseDigitalWaveform*>(GetInputWaveform(1));
	if(!din || !clk)
	{
		LogTrace("no data\n");
		SetData(nullptr, 0);
		SetData(nullptr, 1);
		SetData(nullptr, 2);
		return;
	}

	//Capture the inbound data
	SparseAnalogWaveform sampled;
	SampleOnAnyEdges(din, clk, sampled);

	//Make output waveforms
	auto iout = SetupEmptySparseAnalogOutputWaveform(&sampled, 0);
	auto qout = SetupEmptySparseAnalogOutputWaveform(&sampled, 1);
	auto clkout = SetupEmptySparseDigitalOutputWaveform(&sampled, 2);
	iout->m_triggerPhase = 0;
	qout->m_triggerPhase = 0;
	clkout->m_triggerPhase = 0;

	iout->PrepareForCpuAccess();
	qout->PrepareForCpuAccess();
	clkout->PrepareForCpuAccess();

	//Copy metadata except now using 1fs timesteps
	iout->m_startTimestamp = din->m_startTimestamp;
	iout->m_startFemtoseconds = din->m_startFemtoseconds;
	iout->m_triggerPhase = 0;
	iout->m_timescale = 1;

	qout->m_startTimestamp = din->m_startTimestamp;
	qout->m_startFemtoseconds = din->m_startFemtoseconds;
	qout->m_triggerPhase = 0;
	qout->m_timescale = 1;

	clkout->m_startTimestamp = din->m_startTimestamp;
	clkout->m_startFemtoseconds = din->m_startFemtoseconds;
	clkout->m_triggerPhase = 0;
	clkout->m_timescale = 1;

	size_t len = sampled.size();
	LogTrace("%zu sampled data points\n", len);

	//Figure out the proper I-vs-Q alignment (even/odd is not specified)
	auto align = static_cast<AlignmentType>(m_parameters[m_alignment].GetIntVal());
	size_t istart = 0;
	if(align == ALIGN_100BASET1)
	{
		//Look at a fixed window in the start of the waveform and see which one has the least (0,0) symbols
		size_t window = min(len, (size_t)10000);

		size_t leastZeros = window;
		for(size_t phase=0; phase<2; phase++)
		{
			size_t numSymbols = 0;
			size_t numZeros = 0;

			for(size_t i=phase; i+1 < window; i += 2)
			{
				//For now, fixed threshold of +/- 250 mV for zero code
				auto fi = sampled.m_samples[i];
				auto fq = sampled.m_samples[i+1];

				bool izero = fabs(fi) < 0.25;
				bool qzero = fabs(fq) < 0.25;

				numSymbols ++;
				if(izero && qzero)
					numZeros ++;
			}

			LogTrace("Phase %zu\n", phase);
			LogIndenter li;
			LogTrace("Symbols: %zu\n", numSymbols);
			LogTrace("Zeros:  %zu\n", numZeros);

			if(numZeros < leastZeros)
			{
				istart = phase;
				leastZeros = numZeros;
			}
		}
	}

	//Synthesize the output
	bool clkval = false;
	for(size_t i=istart; i+1 < len; i += 2)
	{
		int64_t tnow = sampled.m_offsets[i];

		//Extend previous sample, if any
		size_t outlen = iout->m_offsets.size();
		if(outlen)
		{
			int64_t dur = tnow - iout->m_offsets[outlen-1];
			iout->m_durations[outlen-1] = dur;
			qout->m_durations[outlen-1] = dur;
			clkout->m_durations[outlen-1] = dur;
		}

		//Add this sample
		iout->m_offsets.push_back(tnow);
		qout->m_offsets.push_back(tnow);
		clkout->m_offsets.push_back(tnow);

		iout->m_durations.push_back(1);
		qout->m_durations.push_back(1);
		clkout->m_durations.push_back(1);

		iout->m_samples.push_back(sampled.m_samples[i]);
		qout->m_samples.push_back(sampled.m_samples[i+1]);
		clkout->m_samples.push_back(clkval);

		clkval = !clkval;
	}

	iout->MarkModifiedFromCpu();
	qout->MarkModifiedFromCpu();
	clkout->MarkModifiedFromCpu();
}
