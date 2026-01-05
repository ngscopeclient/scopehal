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
#include "ClockRecoveryFilter.h"

#ifdef __x86_64__
#include <immintrin.h>
#endif

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ClockRecoveryFilter::ClockRecoveryFilter(const string& color)
	: Filter(color, CAT_CLOCK)
	, m_baudRate(m_parameters["Symbol rate"])
	, m_threshold(m_parameters["Threshold"])
	, m_mtMode(m_parameters["Multithreading"])
{
	AddDigitalStream("recClk");
	AddStream(Unit(Unit::UNIT_VOLTS), "sampledData", Stream::STREAM_TYPE_ANALOG);

	CreateInput("IN");
	CreateInput("Gate");

	m_baudRate = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ));
	m_baudRate.SetFloatVal(1250000000);	//1.25 Gbps

	m_threshold = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_threshold.SetFloatVal(0);

	m_mtMode = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_mtMode.AddEnumValue("CPU single thread", MT_SINGLE_THREAD);
	m_mtMode.AddEnumValue("GPU", MT_GPU);
	m_mtMode.SetIntVal(MT_GPU);

	if(g_hasShaderInt8 && g_hasShaderInt64)
	{
		m_fillSquarewaveAndDurationsComputePipeline =
			make_shared<ComputePipeline>("shaders/FillSquarewaveAndDurations.spv", 3, sizeof(uint32_t));
	}

	if(g_hasShaderInt64 && g_hasShaderInt8)
	{
		m_firstPassComputePipeline =
			make_shared<ComputePipeline>("shaders/ClockRecoveryPLL_FirstPass.spv", 3, sizeof(ClockRecoveryConstants));

		m_secondPassComputePipeline =
			make_shared<ComputePipeline>("shaders/ClockRecoveryPLL_SecondPass.spv", 5, sizeof(ClockRecoveryConstants));

		m_finalPassComputePipeline =
			make_shared<ComputePipeline>("shaders/ClockRecoveryPLL_FinalPass.spv", 9, sizeof(ClockRecoveryConstants));

		//Set up GPU temporary buffers
		m_firstPassTimestamps.SetGpuAccessHint(AcceleratorBuffer<int64_t>::HINT_LIKELY);
		m_firstPassState.SetGpuAccessHint(AcceleratorBuffer<int64_t>::HINT_LIKELY);

		m_secondPassTimestamps.SetGpuAccessHint(AcceleratorBuffer<int64_t>::HINT_LIKELY);
		m_secondPassState.SetGpuAccessHint(AcceleratorBuffer<int64_t>::HINT_LIKELY);
	}
}

ClockRecoveryFilter::~ClockRecoveryFilter()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool ClockRecoveryFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	switch(i)
	{
		case 0:
			if(stream.m_channel == NULL)
				return false;
			return
				(stream.GetType() == Stream::STREAM_TYPE_ANALOG) ||
				(stream.GetType() == Stream::STREAM_TYPE_DIGITAL);

		case 1:
			if(stream.m_channel == NULL)	//null is legal for gate
				return true;

			return (stream.GetType() == Stream::STREAM_TYPE_DIGITAL);

		default:
			return false;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string ClockRecoveryFilter::GetProtocolName()
{
	return "Clock Recovery (PLL)";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void ClockRecoveryFilter::Refresh(
	vk::raii::CommandBuffer& cmdBuf,
	shared_ptr<QueueHandle> queue)
{
	//Require a data signal, but not necessarily a gate
	if(!VerifyInputOK(0))
	{
		SetData(nullptr, 0);
		return;
	}

	auto din = GetInputWaveform(0);
	auto uadin = dynamic_cast<UniformAnalogWaveform*>(din);
	auto sadin = dynamic_cast<SparseAnalogWaveform*>(din);
	auto uddin = dynamic_cast<UniformDigitalWaveform*>(din);
	auto sddin = dynamic_cast<SparseDigitalWaveform*>(din);
	auto gate = GetInputWaveform(1);
	auto sgate = dynamic_cast<SparseDigitalWaveform*>(gate);
	auto ugate = dynamic_cast<UniformDigitalWaveform*>(gate);

	//Get nominal period used for the first cycle of the NCO
	int64_t initialPeriod = round(FS_PER_SECOND / m_baudRate.GetFloatVal());
	int64_t halfPeriod = initialPeriod / 2;

	//Disallow frequencies higher than Nyquist of the input and bail early if we try
	int64_t fnyquist = 2*din->m_timescale;
	if( initialPeriod < fnyquist)
	{
		SetData(nullptr, 0);
		return;
	}

	//If we have a gate signal we're doing a fully CPU based datapath, get ready for that
	if(gate)
		gate->PrepareForCpuAccess();

	//Timestamps of the edges
	size_t nedges = 0;
	AcceleratorBuffer<int64_t> vedges;
	float threshold = m_threshold.GetFloatVal();
	if(uadin)
		nedges = m_detector.FindZeroCrossings(uadin, threshold, cmdBuf, queue);
	else
	{
		din->PrepareForCpuAccess();

		vector<int64_t> edges;
		if(sadin)
			FindZeroCrossings(sadin, threshold, edges);
		else if(uddin)
			FindZeroCrossings(uddin, edges);
		else if(sddin)
			FindZeroCrossings(sddin, edges);
		nedges = edges.size();

		//Inefficient but this is a less frequently used code path
		vedges.resize(nedges);
		vedges.PrepareForCpuAccess();
		memcpy(vedges.GetCpuPointer(), &edges[0], nedges*sizeof(int64_t));
	}
	if(nedges == 0)
	{
		SetData(nullptr, 0);
		return;
	}

	//Edge array
	auto& edges = uadin ? m_detector.GetResults() : vedges;

	//Create the output waveform and copy our timescales
	auto cap = SetupEmptySparseDigitalOutputWaveform(din, 0);
	cap->m_triggerPhase = 0;
	cap->m_timescale = 1;		//recovered clock time scale is single femtoseconds
	cap->m_offsets.reserve(edges.size());

	//If no output data yet, set scales
	if(GetData(1) == nullptr)
	{
		SetVoltageRange(GetInput(0).GetVoltageRange(), 1);
		SetOffset(GetInput(0).GetOffset(), 1);
	}

	//Create analog output waveform for sampled data
	auto scap = SetupEmptySparseAnalogOutputWaveform(din, 1);
	scap->m_triggerPhase = 0;
	scap->m_timescale = 1;		//recovered clock time scale is single femtoseconds

	//Get timestamp of the last sample
	int64_t tend;
	if(uadin)
	{
		//This can be done entirely with metadata and doesn't need to pull samples from the GPU
		tend = GetOffsetScaled(uadin, din->size()-1);
	}
	else if(sadin)
	{
		sadin->PrepareForCpuAccess();
		tend = GetOffsetScaled(sadin, din->size()-1);
	}
	else
	{
		din->PrepareForCpuAccess();
		tend = GetOffsetScaled(sddin, uddin, din->size()-1);
	}

	//The actual PLL NCO
	//TODO: use the real fibre channel PLL.
	bool generatedSquarewaveOnGPU = false;
	if(gate)
	{
		edges.PrepareForCpuAccess();
		cap->PrepareForCpuAccess();
		scap->PrepareForCpuAccess();
		InnerLoopWithGating(*cap, *scap, edges, nedges, tend, initialPeriod, halfPeriod, fnyquist, gate, sgate, ugate);
		cap->m_offsets.MarkModifiedFromCpu();
	}
	else
	{
		//Figure out roughly how many toggles we expect to see in the waveform
		int64_t expectedNumEdges = (tend / initialPeriod);

		//We need a fair number of edges in each thread block for the PLL to lock and not overlap too much
		//For now we assume input is uniformly sampled and fall back if not
		if(	g_hasShaderInt64 && g_hasShaderInt8 &&
			(expectedNumEdges > 100000) && (m_mtMode.GetIntVal() == MT_GPU) &&
			uadin)
		{
			//First pass: run the PLL separately on each chunk of the waveform
			//TODO: do we need to tune numThreads to lock well to short waveforms?
			const uint64_t numThreads = 2048;
			const uint64_t blockSize = 64;
			const uint64_t numBlocks = numThreads / blockSize;
			const uint64_t maxEdges = din->size() / 2;

			//We have no idea how many edges we might generate since the PLL can slew arbitrarily depending on input.
			//The hard upper bound is Nyquist (one edge every 2 input samples) so allocate that much to start
			m_firstPassTimestamps.resize(maxEdges);
			m_secondPassTimestamps.resize(maxEdges);
			cap->Resize(maxEdges);

			//Allocate thread output buffers
			const uint64_t numStateValuesPerThread = 2;
			m_firstPassState.resize(numThreads * numStateValuesPerThread);
			m_secondPassState.resize(numThreads * numStateValuesPerThread);

			cmdBuf.begin({});

			//Constants shared by all passes
			ClockRecoveryConstants cfg;
			cfg.nedges = nedges;
			cfg.fnyquist = fnyquist;
			cfg.maxOffsetsPerThread = maxEdges / numThreads;
			cfg.initialPeriod = initialPeriod;
			cfg.tend = tend;
			cfg.timescale = din->m_timescale;
			cfg.triggerPhase = din->m_triggerPhase;
			cfg.maxInputSamples = din->size();

			//Run the first pass
			m_firstPassComputePipeline->BindBufferNonblocking(0, edges, cmdBuf);
			m_firstPassComputePipeline->BindBufferNonblocking(1, m_firstPassTimestamps, cmdBuf);
			m_firstPassComputePipeline->BindBufferNonblocking(2, m_firstPassState, cmdBuf);
			m_firstPassComputePipeline->Dispatch(cmdBuf, cfg, numBlocks);
			m_firstPassComputePipeline->AddComputeMemoryBarrier(cmdBuf);

			m_firstPassTimestamps.MarkModifiedFromGpu();
			m_firstPassState.MarkModifiedFromGpu();

			//Run the second pass
			m_secondPassComputePipeline->BindBufferNonblocking(0, edges, cmdBuf);
			m_secondPassComputePipeline->BindBufferNonblocking(1, m_firstPassTimestamps, cmdBuf);
			m_secondPassComputePipeline->BindBufferNonblocking(2, m_firstPassState, cmdBuf);
			m_secondPassComputePipeline->BindBufferNonblocking(3, m_secondPassTimestamps, cmdBuf);
			m_secondPassComputePipeline->BindBufferNonblocking(4, m_secondPassState, cmdBuf);
			m_secondPassComputePipeline->Dispatch(cmdBuf, cfg, numBlocks);
			m_secondPassComputePipeline->AddComputeMemoryBarrier(cmdBuf);

			m_secondPassTimestamps.MarkModifiedFromGpu();
			m_secondPassState.MarkModifiedFromGpu();

			scap->m_samples.resize(maxEdges);

			//Run the final pass.
			//This also generates the squarewave output and the sample data
			m_finalPassComputePipeline->BindBufferNonblocking(0, m_firstPassTimestamps, cmdBuf);
			m_finalPassComputePipeline->BindBufferNonblocking(1, m_firstPassState, cmdBuf);
			m_finalPassComputePipeline->BindBufferNonblocking(2, m_secondPassTimestamps, cmdBuf);
			m_finalPassComputePipeline->BindBufferNonblocking(3, m_secondPassState, cmdBuf);
			m_finalPassComputePipeline->BindBufferNonblocking(4, cap->m_offsets, cmdBuf);
			m_finalPassComputePipeline->BindBufferNonblocking(5, cap->m_samples, cmdBuf);
			m_finalPassComputePipeline->BindBufferNonblocking(6, cap->m_durations, cmdBuf);
			m_finalPassComputePipeline->BindBufferNonblocking(7, scap->m_samples, cmdBuf);
			//this assumes input is uniformly sampled for now
			m_finalPassComputePipeline->BindBufferNonblocking(8, uadin->m_samples, cmdBuf);
			m_finalPassComputePipeline->Dispatch(cmdBuf, cfg, numBlocks);

			m_firstPassState.PrepareForCpuAccessNonblocking(cmdBuf);
			m_secondPassState.PrepareForCpuAccessNonblocking(cmdBuf);

			cmdBuf.end();
			queue->SubmitAndBlock(cmdBuf);

			//Figure out how many edges we ended up with
			//TODO: can we avoid this readback?
			uint64_t numSamples = m_firstPassState[0];
			for(uint64_t i=0; i<numThreads; i++)
				numSamples += m_secondPassState[i*2];

			//Output was entirely created on the GPU, no need to touch the CPU for that
			cap->MarkModifiedFromGpu();
			scap->MarkModifiedFromGpu();
			generatedSquarewaveOnGPU = true;

			//Resize to final edge count
			cap->Resize(numSamples);
			scap->Resize(numSamples);

			//Copy the offsets and durations from the sampled data
			scap->m_offsets.CopyFrom(cap->m_offsets, false);
			scap->m_durations.CopyFrom(cap->m_durations, false);
		}

		else
		{
			edges.PrepareForCpuAccess();
			cap->PrepareForCpuAccess();
			InnerLoopWithNoGating(*cap, *scap, edges, nedges, tend, initialPeriod, halfPeriod, fnyquist);
			cap->m_offsets.MarkModifiedFromCpu();
		}
	}

	//Generate the squarewave and duration values to match the calculated timestamps
	if(generatedSquarewaveOnGPU)
	{
		//already done because inner loop was in a shader
	}

	else if(g_hasShaderInt8 && g_hasShaderInt64)
	{
		//Allocate output buffers as needed
		size_t len = cap->m_offsets.size();
		cap->m_samples.resize(len);
		cap->m_durations.resize(len);

		cmdBuf.begin({});

		uint32_t cfg = len;

		m_fillSquarewaveAndDurationsComputePipeline->BindBufferNonblocking(0, cap->m_offsets, cmdBuf);
		m_fillSquarewaveAndDurationsComputePipeline->BindBufferNonblocking(1, cap->m_durations, cmdBuf);
		m_fillSquarewaveAndDurationsComputePipeline->BindBufferNonblocking(2, cap->m_samples, cmdBuf);

		const uint32_t compute_block_count = GetComputeBlockCount(len, 64);
		m_fillSquarewaveAndDurationsComputePipeline->Dispatch(
			cmdBuf,
			cfg,
			min(compute_block_count, 32768u),
			compute_block_count / 32768 + 1);

		cmdBuf.end();

		queue->SubmitAndBlock(cmdBuf);
		cap->MarkModifiedFromGpu();
	}

	//Important to FillDurations() after FillSquarewave() since FillDurations() expects to use sample size
	#ifdef __x86_64__
	else if(g_hasAvx2)
	{
		FillSquarewaveAVX2(*cap);
		FillDurationsAVX2(*cap);
		//FillDurationsAVX2(*scap);

		cap->MarkModifiedFromCpu();
	}
	else
	#endif
	{
		FillSquarewaveGeneric(*cap);
		FillDurationsGeneric(*cap);
		//FillDurationsGeneric(*scap);

		cap->MarkModifiedFromCpu();
	}

	//Generate sampled analog output if not already done GPU side
	if(!generatedSquarewaveOnGPU)
	{
		//TODO: GPU this where possible and don't do a separate sampling pass
		if(uadin)
			SampleOnAnyEdges(uadin, cap, *scap, false);
		else if(sadin)
			SampleOnAnyEdges(sadin, cap, *scap, false);
	}
}

/**
	@brief Fills a waveform with a squarewave
 */
void ClockRecoveryFilter::FillSquarewaveGeneric(SparseDigitalWaveform& cap)
{
	size_t len = cap.m_offsets.size();
	cap.m_samples.resize(len);

	bool value = false;
	for(size_t i=0; i<len; i++)
	{
		value = !value;
		cap.m_samples[i] = value;
	}
}

/**
	@brief Main PLL inner loop supporting an external gate/squelch signal
 */
void ClockRecoveryFilter::InnerLoopWithGating(
	SparseDigitalWaveform& cap,
	[[maybe_unused]] SparseAnalogWaveform& scap,
	AcceleratorBuffer<int64_t>& edges,
	size_t nedges,
	int64_t tend,
	int64_t initialPeriod,
	int64_t halfPeriod,
	int64_t fnyquist,
	WaveformBase* gate,
	SparseDigitalWaveform* sgate,
	UniformDigitalWaveform* ugate)
{
	size_t igate = 0;
	size_t nedge = 1;
	int64_t edgepos = edges[0];
	int64_t period = initialPeriod;

	[[maybe_unused]] int64_t total_error = 0;

	//If gated at T=0, start with output stopped
	bool gating = false;
	if(gate && gate->size())
		gating = !GetValue(sgate, ugate, 0);

	int64_t tlast = 0;
	for(; (edgepos < tend) && (nedge < nedges-1); edgepos += period)
	{
		float center = period/2;

		//See if the current edge position is within a gating region
		bool was_gating = gating;
		if(gate != nullptr)
		{
			while(igate < gate->size()-1)
			{
				//See if this edge is within the region
				int64_t a = GetOffsetScaled(sgate, ugate, igate);
				int64_t b = a + GetDurationScaled(sgate, ugate, igate);

				//We went too far, stop
				if(edgepos < a)
					break;

				//Keep looking
				else if(edgepos > b)
					igate ++;

				//Good alignment
				else
				{
					gating = !GetValue(sgate, ugate, igate);

					//If the clock just got ungated, reset the PLL
					if(!gating && was_gating)
					{
						LogTrace("CDR ungated (at %s)\n", Unit(Unit::UNIT_FS).PrettyPrint(edgepos).c_str());
						LogIndenter li;

						//Find the median pulse width in the next few edges
						//(this is likely either our UI width or an integer multiple thereof)
						vector<int64_t> lengths;
						for(size_t i=1; i<=512; i++)
						{
							if(i + nedge >= nedges)
								break;
							lengths.push_back(edges[nedge+i] - edges[nedge+i-1]);
						}
						std::sort(lengths.begin(), lengths.end());
						auto median = lengths[lengths.size() / 2];
						LogTrace("Median of next %zu edges: %s\n",
							lengths.size(),
							Unit(Unit::UNIT_FS).PrettyPrint(median).c_str());

						//TODO: consider if this might be a multi bit period, rather than the fundamental,
						//depending on the line coding in use? (e.g. TMDS)

						//Look up/down and average everything kinda close to the median (within 25%)
						int64_t sum = 0;
						int64_t navg = 0;
						for(auto w : lengths)
						{
							if( (w >= 0.75*median) && (w <= 1.25*median) )
							{
								sum += w;
								navg ++;
							}
						}
						int64_t avg = sum / navg;
						LogTrace("Average of %lld edges near median: %s\n",
							(long long)navg,
							Unit(Unit::UNIT_FS).PrettyPrint(avg).c_str());

						//For now, assume that this length is our actual pulse width and use it as our period
						period = avg;
						initialPeriod = period;
						halfPeriod = initialPeriod / 2;

						//Align exactly to the next edge
						int64_t tnext = edges[nedge];
						edgepos = tnext + period;
					}

					break;
				}
			}
		}

		//See if the next edge occurred in this UI.
		//If not, just run the NCO open loop.
		//Allow multiple edges in the UI if the frequency is way off.
		int64_t tnext = edges[nedge];
		while( (tnext + center < edgepos) && (nedge+1 < nedges) )
		{
			if(!gating)
			{
				//Find phase error
				int64_t dphase = (edgepos - tnext) - period;

				//If we're more than half a UI off, assume this is actually part of the next UI
				if(dphase > halfPeriod)
					dphase -= period;
				if(dphase < -halfPeriod)
					dphase += period;

				total_error += i64abs(dphase);

				//Find frequency error
				int64_t uiLen = (tnext - tlast);
				float numUIs = round(uiLen * 1.0 / initialPeriod);
				if(numUIs < 0.1)		//Sanity check: no correction if we have a glitch
					uiLen = period;
				else
					uiLen /= numUIs;
				int64_t dperiod = period - uiLen;

				if(tlast != 0)
				{
					//Frequency error term
					period -= dperiod * 0.006;

					//Frequency drift term (delta from refclk)
					//period -= (period - initialPeriod) * 0.0001;

					//Phase error term
					period -= dphase * 0.002;

					//HACK: immediate bang-bang phase shift
					if(dphase > 0)
						edgepos -= period / 400;
					else
						edgepos += period / 400;

					if(period < fnyquist)
					{
						LogWarning("PLL attempted to lock to frequency near or above Nyquist\n");
						nedge = nedges;
						break;
					}
				}
			}

			tlast = tnext;
			tnext = edges[++nedge];
		}

		//Add the sample (90 deg phase offset from the internal NCO)
		if(!gating)
			cap.m_offsets.push_back(edgepos + period/2);
	}

	total_error /= edges.size();
	//LogTrace("average phase error %zu\n", total_error);
}

void ClockRecoveryFilter::InnerLoopWithNoGating(
	SparseDigitalWaveform& cap,
	[[maybe_unused]] SparseAnalogWaveform& scap,
	AcceleratorBuffer<int64_t>& edges,
	size_t nedges,
	int64_t tend,
	int64_t initialPeriod,
	int64_t halfPeriod,
	int64_t fnyquist)
{
	size_t nedge = 1;
	int64_t edgepos = edges[0];

	float initialFrequency = 1.0 / initialPeriod;
	int64_t glitchCutoff = initialPeriod / 10;
	size_t edgemax = nedges - 1;
	float fHalfPeriod = halfPeriod;

	//Predict how many edges we're going to need and allocate space in advance
	//(capture length divided by expected UI length plus 1M extra samples as of margin)
	int64_t expectedNumEdges = (edges[edgemax] / initialPeriod) + 1000000;
	cap.Reserve(expectedNumEdges);

	int64_t tlast = 0;
	int64_t iperiod = initialPeriod;
	float fperiod = iperiod;
	for(; (edgepos < tend) && (nedge < edgemax); edgepos += iperiod)
	{
		int64_t center = iperiod/2;

		//See if the next edge occurred in this UI.
		//If not, just run the NCO open loop.
		//Allow multiple edges in the UI if the frequency is way off.
		int64_t tnext = edges[nedge];
		while( (tnext + center < edgepos) && (nedge < edgemax) )
		{
			//Find phase error
			int64_t dphase = (edgepos - tnext) - iperiod;
			float fdphase = dphase;

			//If we're more than half a UI off, assume this is actually part of the next UI
			if(fdphase > fHalfPeriod)
				fdphase -= fperiod;
			if(fdphase < -fHalfPeriod)
				fdphase += fperiod;

			//Find frequency error
			float uiLen = (tnext - tlast);
			float fdperiod = 0;
			if(uiLen > glitchCutoff)		//Sanity check: no correction if we have a glitch
			{
				float numUIs = roundf(uiLen * initialFrequency);
				if(numUIs != 0)	//divide by zero check needed in some cases
				{
					uiLen /= numUIs;
					fdperiod = fperiod - uiLen;
				}
			}

			if(tlast != 0)
			{
				//Frequency and phase error term
				float errorTerm = (fdperiod * 0.006) + (fdphase * 0.002);
				fperiod -= errorTerm;
				iperiod = fperiod;

				//HACK: immediate bang-bang phase shift
				int64_t bangbang = fperiod * 0.0025;
				if(dphase > 0)
					edgepos -= bangbang;
				else
					edgepos += bangbang;

				if(iperiod < fnyquist)
				{
					LogWarning("PLL attempted to lock to frequency near or above Nyquist\n");
					nedge = nedges;
					break;
				}
			}

			tlast = tnext;
			tnext = edges[++nedge];
		}

		//Add the sample (90 deg phase offset from the internal NCO)
		cap.m_offsets.push_back_nomarkmod(edgepos + center);
	}
}

#ifdef __x86_64__
/**
	@brief AVX2 optimized version of FillSquarewaveGeneric()
 */
__attribute__((target("avx2")))
void ClockRecoveryFilter::FillSquarewaveAVX2(SparseDigitalWaveform& cap)
{
	size_t len = cap.m_offsets.size();
	cap.m_samples.resize(len);
	if(!len)
		return;

	//Load the squarewave dummy fill pattern
	bool filler[32] =
	{
		false, true, false, true, false, true, false, true,
		false, true, false, true, false, true, false, true,
		false, true, false, true, false, true, false, true,
		false, true, false, true, false, true, false, true
	};
	auto fill = _mm256_loadu_si256(reinterpret_cast<__m256i*>(filler));

	size_t end = len - (len % 32);
	uint8_t* ptr = reinterpret_cast<uint8_t*>(&cap.m_samples[0]);
	for(size_t i=0; i<end; i+=32)
		_mm256_storeu_si256(reinterpret_cast<__m256i*>(ptr + i ), fill);

	bool value = false;
	for(size_t i=end; i<len; i++)
	{
		value = !value;
		cap.m_samples[i] = value;
	}
}
#endif /* __x86_64__ */

Filter::DataLocation ClockRecoveryFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}
