/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
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
#include "SDRAMDecoderBase.h"
#include "DramRowColumnLatencyMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DramRowColumnLatencyMeasurement::DramRowColumnLatencyMeasurement(const string& color)
	: Filter(color, CAT_MEASUREMENT)
{
	AddStream(Unit(Unit::UNIT_FS), "trend", Stream::STREAM_TYPE_ANALOG);
	AddStream(Unit(Unit::UNIT_FS), "min", Stream::STREAM_TYPE_ANALOG_SCALAR);
	CreateInput("din");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DramRowColumnLatencyMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i == 0) && (dynamic_cast<SDRAMWaveform*>(stream.m_channel->GetData(stream.m_stream)) != NULL ) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string DramRowColumnLatencyMeasurement::GetProtocolName()
{
	return "DRAM Trcd";
}

Filter::DataLocation DramRowColumnLatencyMeasurement::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DramRowColumnLatencyMeasurement::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("DramRowColumnLatencyMeasurement::Refresh");
	#endif

	ClearErrors();
	if(!VerifyAllInputsOK())
	{
		if(!GetInput(0))
			AddErrorMessage("Missing inputs", "No signal input connected");
		else if(!GetInputWaveform(0))
			AddErrorMessage("Missing inputs", "No waveform available at input");

		SetData(nullptr, 0);
		return;
	}

	//Get the input data
	auto din = dynamic_cast<SDRAMWaveform*>(GetInputWaveform(0));
	din->PrepareForCpuAccess();

	//Create the output
	auto cap = SetupEmptySparseAnalogOutputWaveform(din, 0, true);
	cap->PrepareForCpuAccess();
	cap->m_timescale = 1;

	//Measure delay from activating a row in a bank until a read or write to the same bank
	int64_t lastAct[8] = {0, 0, 0, 0, 0, 0, 0, 0};

	int64_t tlast = 0;
	size_t len = din->m_samples.size();
	float tmin = FLT_MAX;
	for(size_t i=0; i<len; i++)
	{
		int64_t tnow = din->m_offsets[i] * din->m_timescale + din->m_triggerPhase;

		//Discard invalid bank IDs
		auto sample = din->m_samples[i];
		if( (sample.m_bank < 0) || (sample.m_bank > 8) )
			continue;

		//If it's an activate, update the last activation time
		if(sample.m_stype == SDRAMSymbol::TYPE_ACT)
			lastAct[sample.m_bank] = tnow;

		//If it's a read or write, measure the latency
		else if( (sample.m_stype == SDRAMSymbol::TYPE_WR) |
			(sample.m_stype == SDRAMSymbol::TYPE_WRA) |
			(sample.m_stype == SDRAMSymbol::TYPE_RD) |
			(sample.m_stype == SDRAMSymbol::TYPE_RDA) )
		{
			int64_t tcol = tnow;

			//If the activate command is before the start of the capture, ignore this event
			int64_t tact = lastAct[sample.m_bank];
			if(tact == 0)
				continue;

			//Valid access, measure the latency
			int64_t delta = tcol - tact;
			if(!cap->m_durations.empty())
				cap->m_durations[cap->m_durations.size()-1] = tnow - tlast;
			cap->m_offsets.push_back(tnow);
			cap->m_durations.push_back(1);
			cap->m_samples.push_back(delta);
			tmin = min(tmin, (float)delta);
			tlast = tnow;

			//Purge the last-refresh activate so we don't report false times for the next read or write
			lastAct[sample.m_bank] = 0;
		}
	}

	if(cap->m_samples.empty())
	{
		delete cap;
		SetData(nullptr, 0);
		return;
	}

	m_streams[1].m_value = tmin;

	cap->MarkModifiedFromCpu();
}
