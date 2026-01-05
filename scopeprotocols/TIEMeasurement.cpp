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
#include "TIEMeasurement.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

TIEMeasurement::TIEMeasurement(const string& color)
	: Filter(color, CAT_CLOCK)
	, m_threshold(m_parameters["Threshold"])
	, m_skipStart(m_parameters["Skip Start"])
{
	AddStream(Unit(Unit::UNIT_FS), "data", Stream::STREAM_TYPE_ANALOG);

	//Set up channels
	CreateInput("Clock");
	CreateInput("Golden");

	m_threshold = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_threshold.SetFloatVal(0);

	m_skipStart = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));
	m_skipStart.SetIntVal(0);

	m_clockEdgesMuxed = nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool TIEMeasurement::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;
	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )//allow digital clocks
		return true;
	if( (i == 1) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string TIEMeasurement::GetProtocolName()
{
	return "Clock Jitter (TIE)";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void TIEMeasurement::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	if(!VerifyAllInputsOK())
	{
		SetData(nullptr, 0);
		return;
	}

	//Get the input data
	auto clk = GetInputWaveform(0);
	auto uaclk = dynamic_cast<UniformAnalogWaveform*>(clk);
	auto saclk = dynamic_cast<SparseAnalogWaveform*>(clk);
	auto udclk = dynamic_cast<UniformDigitalWaveform*>(clk);
	auto sdclk = dynamic_cast<SparseDigitalWaveform*>(clk);
	auto golden = GetInputWaveform(1);
	auto sgolden = dynamic_cast<SparseDigitalWaveform*>(golden);
	auto ugolden = dynamic_cast<UniformDigitalWaveform*>(golden);
	size_t len = min(clk->size(), golden->size());

	golden->PrepareForCpuAccess();

	//Create the output
	auto cap = SetupEmptySparseAnalogOutputWaveform(clk, 0);
	cap->m_timescale = 1;
	cap->m_triggerPhase = 0;
	cap->PrepareForCpuAccess();

	//Timestamps of the edges
	//Fast path: GPU edge detection on uniform input
	if(uaclk)
	{
		m_detector.FindZeroCrossings(uaclk, m_threshold.GetFloatVal(), cmdBuf, queue);
		m_clockEdgesMuxed = &m_detector.GetResults();
	}

	//Slow path: look for edges on the CPU
	else
	{
		clk->PrepareForCpuAccess();
		vector<int64_t> clock_edges;
		if(sdclk || udclk)
			FindZeroCrossings(sdclk, udclk, clock_edges);
		else
			FindZeroCrossings(saclk, uaclk, m_threshold.GetFloatVal(), clock_edges);
		m_clockEdges.CopyFrom(clock_edges);
		m_clockEdgesMuxed = &m_clockEdges;
	}

	//Ignore edges before things have stabilized
	int64_t skip_time = m_skipStart.GetIntVal();

	//For each input clock edge, find the closest recovered clock edge
	//For now, this is all CPU side
	size_t iedge = 0;
	size_t tlast = 0;
	m_clockEdgesMuxed->PrepareForCpuAccess();
	for(auto atime : *m_clockEdgesMuxed)
	{
		if(iedge >= len)
			break;

		int64_t prev_edge = ::GetOffsetScaled(sgolden, ugolden, iedge);
		int64_t next_edge = prev_edge;
		size_t jedge = iedge;

		bool hit = false;

		//Look for a pair of edges bracketing our edge
		while(true)
		{
			prev_edge = next_edge;
			next_edge = ::GetOffsetScaled(sgolden, ugolden, jedge);

			//First golden edge is after this signal edge
			if(prev_edge > atime)
				break;

			//Bracketed
			if( (prev_edge < atime) && (next_edge > atime) )
			{
				hit = true;
				break;
			}

			//No, keep looking
			jedge ++;

			//End of capture
			if(jedge >= len)
				break;
		}

		//No interval error possible without a reference clock edge.
		if(!hit)
			continue;

		//Hit! We're bracketed. Start the next search from this edge
		iedge = jedge;

		//Since the CDR filter adds a 90 degree phase offset for sampling in the middle of the data eye,
		//we need to use the *midpoint* of the golden clock cycle as the nominal position of the clock
		//edge for TIE measurements.
		int64_t golden_period = next_edge - prev_edge;
		int64_t golden_center = prev_edge + golden_period/2;
		int64_t tie = atime - golden_center;

		//Ignore edges before things have stabilized
		if(prev_edge < skip_time)
		{}

		else
		{
			//Update the last sample
			size_t end = cap->size();
			if(end)
				cap->m_durations[end-1] = atime - tlast;

			cap->m_offsets.push_back(golden_center);
			cap->m_durations.push_back(0);
			cap->m_samples.push_back(tie);
		}

		tlast = golden_center;
	}

	cap->MarkModifiedFromCpu();
}
