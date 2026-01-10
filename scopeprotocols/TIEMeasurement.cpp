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
#include "ClockRecoveryFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

TIEMeasurement::TIEMeasurement(const string& color)
	: Filter(color, CAT_CLOCK)
	, m_threshold(m_parameters["Threshold"])
	, m_skipStart(m_parameters["Skip Start"])
	, m_firstPassOutput("TIEMeasurement.firstPassOutput")
	, m_secondPassOutput("TIEMeasurement.secondPassOutput")
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

	if(g_hasShaderInt64)
	{
		m_firstPassComputePipeline =
			make_shared<ComputePipeline>("shaders/TIEMeasurement_FirstPass.spv", 3, sizeof(TIEConstants));
		m_secondPassComputePipeline =
			make_shared<ComputePipeline>("shaders/TIEMeasurement_SecondPass.spv", 5, sizeof(TIEConstants));

		m_firstPassOutput.SetGpuAccessHint(AcceleratorBuffer<int64_t>::HINT_LIKELY);
		m_secondPassOutput.resize(1);
	}
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

Filter::DataLocation TIEMeasurement::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void TIEMeasurement::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range range("TIEMeasurement::Refresh");
	#endif

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

	//Create the output
	auto cap = SetupEmptySparseAnalogOutputWaveform(clk, 0);
	cap->m_timescale = 1;
	cap->m_triggerPhase = 0;

	//Fastest path: if our reference signal was fed to the CDR PLL driving our golden input,
	//it's already been edge detected. Use those edges instead!
	float threshold = m_threshold.GetFloatVal();
	auto pcdr = dynamic_cast<ClockRecoveryFilter*>(GetInput(1).m_channel);
	if(pcdr && (fabs(pcdr->GetThreshold() - threshold) < 0.01) && (pcdr->GetInput(0) == GetInput(0)) && uaclk)
		m_clockEdgesMuxed = &pcdr->GetZeroCrossings();

	//Normal fast path: GPU edge detection on uniform input
	else if(uaclk)
	{
		m_detector.FindZeroCrossings(uaclk, threshold, cmdBuf, queue);
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
			FindZeroCrossings(saclk, uaclk, threshold, clock_edges);
		m_clockEdges.CopyFrom(clock_edges);
		m_clockEdgesMuxed = &m_clockEdges;
	}

	//Ignore edges before things have stabilized
	int64_t skip_time = m_skipStart.GetIntVal();

	//To start: reserve one sample for each input edge (may not end up with that many but it's an upper bound)
	cap->Resize(m_clockEdgesMuxed->size());

	//For each input clock edge, find the closest recovered clock edge
	//Fast path: golden clock came from CDR filter and we have GPU native int64 support
	if(g_hasShaderInt64 && pcdr && sgolden)
	{
		cmdBuf.begin({});

		//Allocate output buffer, we know there should be one edge for each input sample at max
		//Entry 0 is number of edges written
		//Entry 2i + 1 is offset
		//Entry 2i + 2 is TIE
		const uint32_t numThreads = 16384;
		const uint32_t threadsPerBlock = 64;
		const uint32_t numBlocks = numThreads / threadsPerBlock;
		const uint32_t maxEdgesPerThread = GetComputeBlockCount(m_clockEdgesMuxed->size(), numThreads);
		const uint32_t blockBufferSize = 2*maxEdgesPerThread + 1;
		m_firstPassOutput.resize(blockBufferSize * numThreads);

		//Push constants
		TIEConstants cfg;
		cfg.nedges = m_clockEdgesMuxed->size();
		cfg.ngolden = sgolden->m_offsets.size();
		cfg.blockBufferSize = blockBufferSize;
		cfg.skip_time = skip_time;
		cfg.maxEdgesPerThread = maxEdgesPerThread;

		//Run the first pass
		m_firstPassComputePipeline->BindBufferNonblocking(0, *m_clockEdgesMuxed, cmdBuf);
		m_firstPassComputePipeline->BindBufferNonblocking(1, sgolden->m_offsets, cmdBuf);
		m_firstPassComputePipeline->BindBufferNonblocking(2, m_firstPassOutput, cmdBuf);
		m_firstPassComputePipeline->Dispatch(cmdBuf, cfg, numBlocks);
		m_firstPassComputePipeline->AddComputeMemoryBarrier(cmdBuf);
		m_firstPassOutput.MarkModifiedFromGpu();

		//Second pass: merge the outputs of the first pass and calculate durations
		m_secondPassComputePipeline->BindBufferNonblocking(0, m_firstPassOutput, cmdBuf);
		m_secondPassComputePipeline->BindBufferNonblocking(1, cap->m_offsets, cmdBuf);
		m_secondPassComputePipeline->BindBufferNonblocking(2, cap->m_durations, cmdBuf);
		m_secondPassComputePipeline->BindBufferNonblocking(3, cap->m_samples, cmdBuf);
		m_secondPassComputePipeline->BindBufferNonblocking(4, m_secondPassOutput, cmdBuf);
		m_secondPassComputePipeline->Dispatch(cmdBuf, cfg, numBlocks);
		m_secondPassOutput.MarkModifiedFromGpu();
		cap->MarkModifiedFromGpu();

		m_secondPassOutput.PrepareForCpuAccessNonblocking(cmdBuf);

		cmdBuf.end();
		queue->SubmitAndBlock(cmdBuf);

		//Update final sample count
		cap->Resize(m_secondPassOutput[0]);
	}
	else if(pcdr && sgolden)
	{
		cap->PrepareForCpuAccess();
		golden->PrepareForCpuAccess();

		size_t iedge = 0;
		size_t tlast = 0;
		size_t nedge = 0;
		m_clockEdgesMuxed->PrepareForCpuAccess();
		for(auto atime : *m_clockEdgesMuxed)
		{
			if(iedge >= len)
				break;

			int64_t prev_edge = sgolden->m_offsets[iedge];
			int64_t next_edge = prev_edge;
			size_t jedge = iedge;

			bool hit = false;

			//Look for a pair of edges bracketing our edge
			while(true)
			{
				prev_edge = next_edge;
				next_edge = sgolden->m_offsets[jedge];

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

			//Ignore edges before things have stabilized
			if(prev_edge < skip_time)
			{}

			else
			{
				//Set duration of the last sample
				if(nedge !=  0)
					cap->m_durations[nedge-1] = atime - tlast;

				//Add a new sample
				cap->m_offsets[nedge] = golden_center;
				cap->m_samples[nedge] = atime - golden_center;
				nedge ++;
			}

			tlast = golden_center;
		}

		//Set duration for the last sample and adjust final size
		cap->m_durations[nedge-1] = 1;
		cap->Resize(nedge);
		cap->MarkModifiedFromCpu();
	}

	//Slow path: CPU fallback
	else
	{
		cap->PrepareForCpuAccess();
		golden->PrepareForCpuAccess();

		size_t iedge = 0;
		size_t tlast = 0;
		size_t nedge = 0;
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

			//Ignore edges before things have stabilized
			if(prev_edge < skip_time)
			{}

			else
			{
				//Set duration of the last sample
				if(nedge !=  0)
					cap->m_durations[nedge-1] = atime - tlast;

				//Add a new sample
				cap->m_offsets[nedge] = golden_center;
				cap->m_samples[nedge] = atime - golden_center;
				nedge ++;
			}

			tlast = golden_center;
		}

		//Set duration for the last sample and adjust final size
		cap->m_durations[nedge-1] = 1;
		cap->Resize(nedge);
		cap->MarkModifiedFromCpu();
	}
}
