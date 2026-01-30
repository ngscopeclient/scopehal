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
#include "FrequencyMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

FrequencyMeasurement::FrequencyMeasurement(const string& color)
	: Filter(color, CAT_MEASUREMENT)
{
	AddStream(Unit(Unit::UNIT_HZ), "trend", Stream::STREAM_TYPE_ANALOG);
	AddStream(Unit(Unit::UNIT_HZ), "avg", Stream::STREAM_TYPE_ANALOG_SCALAR);

	//Set up channels
	CreateInput("din");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool FrequencyMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if(i > 0)
		return false;

	if( (stream.GetType() == Stream::STREAM_TYPE_ANALOG) ||
		(stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
	{
		return true;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string FrequencyMeasurement::GetProtocolName()
{
	return "Frequency";
}

Filter::DataLocation FrequencyMeasurement::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void FrequencyMeasurement::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("FrequencyMeasurement::Refresh");
	#endif

	//Make sure we've got valid inputs
	ClearErrors();
	if(!VerifyAllInputsOK())
	{
		if(!GetInput(0))
			AddErrorMessage("Missing inputs", "No signal input connected");
		else if(!GetInputWaveform(0))
			AddErrorMessage("Missing inputs", "No waveform available at input");

		SetData(nullptr, 0);
		m_streams[1].m_value = NAN;
		return;
	}

	auto din = GetInputWaveform(0);
	auto uadin = dynamic_cast<UniformAnalogWaveform*>(din);
	auto sadin = dynamic_cast<SparseAnalogWaveform*>(din);
	auto uddin = dynamic_cast<UniformDigitalWaveform*>(din);
	auto sddin = dynamic_cast<SparseDigitalWaveform*>(din);

	//Auto-threshold analog signals at 50% of full scale range
	if(uadin)
		m_detector.FindZeroCrossings(uadin, m_averager.Average(uadin, cmdBuf, queue), cmdBuf, queue);
	else if(sadin)
		m_detector.FindZeroCrossings(sadin, m_averager.Average(sadin, cmdBuf, queue), cmdBuf, queue);

	//Just find edges in digital signals
	else if(uddin)
		m_detector.FindZeroCrossings(uddin, cmdBuf, queue);
	else
		m_detector.FindZeroCrossings(sddin, cmdBuf, queue);

	auto& edges = m_detector.GetResults();

	//We need at least one full cycle of the waveform to have a meaningful frequency
	size_t elen = edges.size();
	if(elen < 2)
	{
		AddErrorMessage("Input too short", "Need at least two edges for a meaningful frequency measurement");
		SetData(nullptr, 0);
		m_streams[1].m_value = NAN;
		return;
	}

	//Create the output
	size_t outlen = (elen-2) / 2;
	auto cap = SetupEmptySparseAnalogOutputWaveform(din, 0, true);
	cap->m_timescale = 1;
	cap->Resize(outlen);

	//TODO: GPU inner loop
	cap->PrepareForCpuAccess();
	edges.PrepareForCpuAccess();
	for(size_t i=0; i < outlen; i++)
	{
		//measure from edge to 2 edges later, since we find all zero crossings regardless of polarity
		int64_t start = edges[i*2];
		int64_t end = edges[i*2 + 2];

		int64_t delta = end - start;
		double freq = FS_PER_SECOND / delta;

		cap->m_offsets[i] = start;
		cap->m_durations[i] = delta;
		cap->m_samples[i] = round(freq);
	}
	cap->MarkModifiedFromCpu();

	//For the scalar average output, find the total number of zero crossings and divide by the spacing
	//(excluding partial cycles at start and end).
	//This gives us twice our frequency (since we count both zero crossings) so divide by two again
	double ncycles = elen - 1;
	double interval = edges[elen-1] - edges[0];
	m_streams[1].m_value = ncycles / (2 * interval * SECONDS_PER_FS);
}
