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
#include "PAMEdgeDetectorFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PAMEdgeDetectorFilter::PAMEdgeDetectorFilter(const string& color)
	: Filter(color, CAT_CLOCK)
	, m_order(m_parameters["PAM Order"])
	, m_baud(m_parameters["Symbol rate"])
{
	AddDigitalStream("data");

	CreateInput("din");

	m_order = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_order.SetIntVal(3);

	m_baud = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_HZ));
	m_baud.SetIntVal(1250000000);	//1.25 Gbps

	if(g_hasShaderInt8 && g_hasShaderInt64)
	{
		m_edgeIndexes.SetGpuAccessHint(AcceleratorBuffer<uint32_t>::HINT_LIKELY);

		m_edgeIndexesScratch.SetGpuAccessHint(AcceleratorBuffer<uint32_t>::HINT_LIKELY);
		m_edgeStatesScratch.SetGpuAccessHint(AcceleratorBuffer<uint8_t>::HINT_LIKELY);
		m_edgeRisingScratch.SetGpuAccessHint(AcceleratorBuffer<uint8_t>::HINT_LIKELY);

		//Use pinned memory for output buffers to avoid separate copy operations
		m_edgeCount.SetGpuAccessHint(AcceleratorBuffer<uint32_t>::HINT_UNLIKELY);
		m_edgeCount.resize(1);

		m_thresholds.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
		m_levels.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);

		m_firstPassComputePipeline =
			make_shared<ComputePipeline>("shaders/PAMEdgeDetector_LevelCrossings.spv", 5, sizeof(PAMEdgeDetectorConstants));

		m_secondPassComputePipeline =
			make_shared<ComputePipeline>("shaders/PAMEdgeDetector_MergeCrossings.spv", 7, sizeof(PAMEdgeDetectorConstants));

		m_initialMergeComputePipeline =
			make_shared<ComputePipeline>("shaders/PAMEdgeDetector_InitialMerge.spv", 6, sizeof(PAMEdgeDetectorMergeConstants));

		m_finalMergeComputePipeline =
			make_shared<ComputePipeline>("shaders/PAMEdgeDetector_FinalMerge.spv", 5, sizeof(PAMEdgeDetectorMergeConstants));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool PAMEdgeDetectorFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string PAMEdgeDetectorFilter::GetProtocolName()
{
	return "PAM Edge Detector";
}

Filter::DataLocation PAMEdgeDetectorFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void PAMEdgeDetectorFilter::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("PAMEdgeDetectorFilter::Refresh");
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
	auto din = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(0));
	if(!din)
	{
		AddErrorMessage("Missing inputs", "No uniform analog waveform available at input");
		SetData(nullptr, 0);
		return;
	}
	auto len = din->size();

	int64_t ui = round(FS_PER_SECOND / m_baud.GetIntVal());
	size_t order = m_order.GetIntVal();

	//Extract parameter values for input thresholds
	m_levels.resize(0);
	m_levels.PrepareForCpuAccess();
	for(size_t i=0; i<order; i++)
	{
		//If no threshold available, autofit
		auto pname = string("Level ") + to_string(i);
		if(m_parameters.find(pname) == m_parameters.end())
		{
			din->PrepareForCpuAccess();
			AutoLevel(din);
		}

		//Extract the level
		m_levels.push_back(m_parameters[pname].GetFloatVal());
	}
	m_levels.MarkModifiedFromCpu();

	//Decision thresholds for initial symbol assignment
	//This is fast so no need to cache
	vector<float> sthresholds;
	sthresholds.resize(order-1);
	for(size_t i=0; i<order-1; i++)
	{
		float from = m_levels[i];
		float to = m_levels[i+1];
		sthresholds[i] = (from + to) / 2;
	}

	//Output waveform is sparse since we interpolate edge positions
	auto cap = SetupEmptySparseDigitalOutputWaveform(din, 0);
	cap->m_timescale = 1;
	cap->m_triggerPhase = 0;

	//Grab temporary buffers we're going to use
	ScratchBuffer_uint8_t edgeStates(ScratchBufferManager::U8_GPU_WAVEFORM);
	ScratchBuffer_uint8_t edgeRising(ScratchBufferManager::U8_GPU_WAVEFORM);

	if(g_hasShaderInt64 && g_hasShaderInt8)
	{
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// First shader pass

		//Find *all* level crossings
		//This will double-count some edges (e.g. a +1 to -1 edge will show up as +1 to 0 and 0 to -1)

		//Prepare thresholds for GPU
		//TODO: only if changed
		m_thresholds.PrepareForCpuAccess();
		m_thresholds.resize(order-1);
		for(size_t i=0; i<sthresholds.size(); i++)
			m_thresholds[i] = sthresholds[i];
		m_thresholds.MarkModifiedFromCpu();

		//Allocate output space
		m_edgeIndexesScratch.resize(len);
		m_edgeStatesScratch.resize(len);
		m_edgeRisingScratch.resize(len);

		m_edgeIndexes.resize(len);
		edgeStates->resize(len);
		edgeRising->resize(len);

		cmdBuf.begin({});

		uint64_t numThreads = 8192;
		uint64_t blockSize = 64;
		uint64_t numBlocks = numThreads / blockSize;

		//Constants shared by all passes
		PAMEdgeDetectorConstants cfg;
		cfg.len = len;
		cfg.order = order;
		cfg.inputPerThread = GetComputeBlockCount(len, numThreads);
		cfg.outputPerThread = cfg.inputPerThread;

		//Run the first pass
		m_firstPassComputePipeline->BindBufferNonblocking(0, din->m_samples, cmdBuf);
		m_firstPassComputePipeline->BindBufferNonblocking(1, m_thresholds, cmdBuf);
		m_firstPassComputePipeline->BindBufferNonblocking(2, m_edgeIndexesScratch, cmdBuf, true);
		m_firstPassComputePipeline->BindBufferNonblocking(3, m_edgeStatesScratch, cmdBuf, true);
		m_firstPassComputePipeline->BindBufferNonblocking(4, m_edgeRisingScratch, cmdBuf, true);
		m_firstPassComputePipeline->Dispatch(cmdBuf, cfg, numBlocks);
		m_firstPassComputePipeline->AddComputeMemoryBarrier(cmdBuf);

		m_edgeIndexesScratch.MarkModifiedFromGpu();
		m_edgeStatesScratch.MarkModifiedFromGpu();
		m_edgeRisingScratch.MarkModifiedFromGpu();

		//Run the second pass
		m_secondPassComputePipeline->BindBufferNonblocking(0, m_edgeIndexesScratch, cmdBuf);
		m_secondPassComputePipeline->BindBufferNonblocking(1, m_edgeStatesScratch, cmdBuf);
		m_secondPassComputePipeline->BindBufferNonblocking(2, m_edgeRisingScratch, cmdBuf);
		m_secondPassComputePipeline->BindBufferNonblocking(3, m_edgeIndexes, cmdBuf, true);
		m_secondPassComputePipeline->BindBufferNonblocking(4, *edgeStates, cmdBuf, true);
		m_secondPassComputePipeline->BindBufferNonblocking(5, *edgeRising, cmdBuf, true);
		m_secondPassComputePipeline->BindBufferNonblocking(6, m_edgeCount, cmdBuf, true);
		m_secondPassComputePipeline->Dispatch(cmdBuf, cfg, numBlocks);
		m_secondPassComputePipeline->AddComputeMemoryBarrier(cmdBuf);

		m_edgeIndexes.MarkModifiedFromGpu();
		edgeStates->MarkModifiedFromGpu();
		edgeRising->MarkModifiedFromGpu();
		m_edgeCount.MarkModifiedFromGpu();

		m_edgeCount.PrepareForCpuAccessNonblocking(cmdBuf);

		cmdBuf.end();
		queue->SubmitAndBlock(cmdBuf);

		uint32_t numSamples = m_edgeCount[0];
		m_edgeIndexes.resize(numSamples);
		edgeStates->resize(numSamples);
		edgeRising->resize(numSamples);

		LogTrace("First pass: Found %zu level crossings\n", m_edgeIndexes.size());

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Second shader pass: merge everything

		cmdBuf.begin({});

		numThreads = 4096;
		blockSize = 64;
		numBlocks = numThreads / blockSize;

		//Push constants
		PAMEdgeDetectorMergeConstants mergecfg;
		mergecfg.halfui = ui / 2;
		mergecfg.timescale = din->m_timescale;
		mergecfg.numIndexes = m_edgeIndexes.size();
		mergecfg.numSamples = din->size();
		mergecfg.inputPerThread = GetComputeBlockCount(mergecfg.numIndexes, numThreads);
		mergecfg.outputPerThread = mergecfg.inputPerThread + 1;
		mergecfg.order = order;
		mergecfg.triggerPhase = din->m_triggerPhase;

		ScratchBuffer_int64_t edgeOffsetsScratch(ScratchBufferManager::I64_GPU_WAVEFORM);
		edgeOffsetsScratch->resize(mergecfg.outputPerThread * numThreads);

		//Run the first pass
		m_initialMergeComputePipeline->BindBufferNonblocking(0, m_edgeIndexes, cmdBuf);
		m_initialMergeComputePipeline->BindBufferNonblocking(1, *edgeStates, cmdBuf);
		m_initialMergeComputePipeline->BindBufferNonblocking(2, *edgeRising, cmdBuf);
		m_initialMergeComputePipeline->BindBufferNonblocking(3, din->m_samples, cmdBuf);
		m_initialMergeComputePipeline->BindBufferNonblocking(4, m_levels, cmdBuf);
		m_initialMergeComputePipeline->BindBufferNonblocking(5, *edgeOffsetsScratch, cmdBuf, true);
		m_initialMergeComputePipeline->Dispatch(cmdBuf, mergecfg, numBlocks);
		m_initialMergeComputePipeline->AddComputeMemoryBarrier(cmdBuf);

		edgeOffsetsScratch->MarkModifiedFromGpu();

		//Reserve space in the output buffer (this is an overestimate but will be corrected)
		cap->Resize(mergecfg.outputPerThread * numThreads);

		//Run the final pass
		m_finalMergeComputePipeline->BindBufferNonblocking(0, *edgeOffsetsScratch, cmdBuf);
		m_finalMergeComputePipeline->BindBufferNonblocking(1, cap->m_offsets, cmdBuf, true);
		m_finalMergeComputePipeline->BindBufferNonblocking(2, cap->m_durations, cmdBuf, true);
		m_finalMergeComputePipeline->BindBufferNonblocking(3, cap->m_samples, cmdBuf, true);
		m_finalMergeComputePipeline->BindBufferNonblocking(4, m_edgeCount, cmdBuf, true);

		m_finalMergeComputePipeline->Dispatch(cmdBuf, mergecfg, 1, /*numBlocks*/numThreads);
		m_finalMergeComputePipeline->AddComputeMemoryBarrier(cmdBuf);

		cap->MarkModifiedFromGpu();
		m_edgeCount.MarkModifiedFromGpu();

		m_edgeCount.PrepareForCpuAccessNonblocking(cmdBuf);

		cmdBuf.end();
		queue->SubmitAndBlock(cmdBuf);

		//Get final edge count
		cap->Resize(m_edgeCount[0]);
	}

	else
	{
		m_edgeIndexes.clear();
		edgeStates->clear();
		edgeRising->clear();

		m_edgeIndexes.reserve(len);
		edgeStates->reserve(len);
		edgeRising->reserve(len);

		m_edgeIndexes.PrepareForCpuAccess();
		edgeStates->PrepareForCpuAccess();
		edgeRising->PrepareForCpuAccess();

		din->PrepareForCpuAccess();

		for(size_t i=1; i<len-1; i++)
		{
			//Check against each threshold for both rising and falling edges
			float prev = din->m_samples[i-1];
			float cur = din->m_samples[i];

			//Prepare to make a new edge
			for(size_t j=0; j<sthresholds.size(); j++)
			{
				float t = sthresholds[j];

				//Check for rising edge
				if( (prev <= t) && (cur > t) )
				{
					m_edgeIndexes.push_back(i);
					edgeRising->push_back(1);
					edgeStates->push_back(j+1);
					break;
				}

				//Check for falling edge
				else if( (prev >= t) && (cur < t) )
				{
					m_edgeIndexes.push_back(i);
					edgeRising->push_back(0);
					edgeStates->push_back(j);
					break;
				}

				//else not a level crossing
			}
		}

		m_edgeIndexes.MarkModifiedFromCpu();
		edgeStates->MarkModifiedFromCpu();
		edgeRising->MarkModifiedFromCpu();

		LogTrace("First pass: Found %zu level crossings\n", m_edgeIndexes.size());

		cap->PrepareForCpuAccess();

		//Add initial dummy sample at time zero
		cap->m_offsets.push_back(0);
		cap->m_durations.push_back(1);
		cap->m_samples.push_back(0);

		//Loop over level crossings and figure out what they are
		int64_t halfui = ui / 2;
		bool nextValue = true;
		for(size_t i=0; i<m_edgeIndexes.size(); i++)
		{
			size_t istart = m_edgeIndexes[i] - 1;
			size_t iend = m_edgeIndexes[i] + 1;
			size_t symstart;
			size_t symend = (*edgeStates)[i];

			//If our first sample occurs too early in the waveform, we can't interpolate. Skip it.
			if(istart == 0)
				continue;

			if((*edgeRising)[i])
				symstart = symend - 1;
			else
				symstart = symend + 1;

			//If the previous edge is close to this one (< 0.5 UI)
			//and they're both rising or falling, merge them
			bool merging = false;
			for(size_t lookback = 1; lookback < order-1; lookback ++)
			{
				if(i <= lookback)
					break;

				int64_t delta = (m_edgeIndexes[i] - m_edgeIndexes[i-lookback]) * din->m_timescale;
				if( ((*edgeRising)[i-lookback] == (*edgeRising)[i]) && (delta < halfui) )
				{
					merging = true;
					istart = m_edgeIndexes[i-lookback]-1;

					if((*edgeRising)[i])
						symstart = symend - (lookback+1);
					else
						symstart = symend + (lookback+1);
				}
				else
					break;
			}

			//Find the midpoint (for now, fixed threshold still)
			float target = (m_levels[symstart] + m_levels[symend]) / 2;
			int64_t tlerp = 0;
			for(size_t j=istart; j<iend; j++)
			{
				float prev = din->m_samples[j-1];
				float cur = din->m_samples[j];

				if(	( (prev <= target) && (cur > target) ) ||
					( (prev >= target) && (cur < target) ) )
				{
					tlerp = (j-1)*din->m_timescale;
					tlerp += (int64_t)(InterpolateTime(din, j-1, target)*din->m_timescale);
					break;
				}
			}
			tlerp += din->m_triggerPhase;

			//Add the symbol
			if(!merging)
			{
				cap->m_offsets.push_back(tlerp);
				cap->m_durations.push_back(1);
				cap->m_samples.push_back(nextValue);

				//Extend previous sample, if any
				size_t outlen = cap->m_offsets.size();
				if(outlen)
					cap->m_durations[outlen-1] = tlerp - cap->m_offsets[outlen-1];

				nextValue = !nextValue;
			}

			//Overwrite previous sample
			else
			{
				size_t outlen = cap->m_offsets.size();
				cap->m_offsets[outlen-1] = tlerp;

				//Update duration of previous sample
				if(outlen > 1)
					cap->m_durations[outlen-2] = tlerp - cap->m_offsets[outlen-2];
			}
		}

		cap->MarkModifiedFromCpu();
	}
}

vector<string> PAMEdgeDetectorFilter::EnumActions()
{
	vector<string> ret;
	ret.push_back("Auto Level");
	return ret;
}

bool PAMEdgeDetectorFilter::PerformAction(const string& id)
{
	if(id == "Auto Level")
	{
		auto din = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(0));
		if(din)
		{
			din->PrepareForCpuAccess();
			AutoLevel(din);
			return true;
		}
	}
	return false;
}

void PAMEdgeDetectorFilter::AutoLevel(UniformAnalogWaveform* din)
{
	size_t order = m_order.GetIntVal();

	float vmin, vmax;
	GetMinMaxVoltage(din, vmin, vmax);
	Unit yunit(Unit::UNIT_VOLTS);
	LogTrace("Bounds are %s to %s\n", yunit.PrettyPrint(vmin).c_str(), yunit.PrettyPrint(vmax).c_str());

	//Take a histogram and find the top N peaks (should be roughly evenly distributed)
	const int64_t nbins = 250;
	auto hist = MakeHistogram(din, vmin, vmax, nbins);
	float binsize = (vmax - vmin) / nbins;

	//Search radius for bins (for now hard code, TODO make this adaptive?)
	const int64_t searchrad = 10;
	ssize_t nend = nbins - 1;
	vector<Peak> peaks;
	for(ssize_t i=searchrad; i<(nbins - searchrad); i++)
	{
		//Locate the peak
		ssize_t left = std::max((ssize_t)searchrad, (ssize_t)(i - searchrad));
		ssize_t right = std::min((ssize_t)(i + searchrad), (ssize_t)nend);

		float target = hist[i];
		bool is_peak = true;
		for(ssize_t j=left; j<=right; j++)
		{
			if(i == j)
				continue;
			if(hist[j] >= target)
			{
				//Something higher is to our right.
				//It's higher than anything from left to j. This makes it a candidate peak.
				//Restart our search from there.
				if(j > i)
					i = j-1;

				is_peak = false;
				break;
			}
		}
		if(!is_peak)
			continue;

		//Do a weighted average of our immediate neighbors to fine tune our position
		ssize_t fine_rad = 10;
		left = std::max((ssize_t)1, i - fine_rad);
		right = std::min(i + fine_rad, nend);
		double total = 0;
		double count = 0;
		for(ssize_t j=left; j<=right; j++)
		{
			total += j*hist[j];
			count += hist[j];
		}
		peaks.push_back(Peak(round(total / count), target, 1));
	}

	//Sort the peak table by height and pluck out the requested count, use these as our levels
	std::sort(peaks.rbegin(), peaks.rend(), std::less<Peak>());
	vector<float> levels;
	if(peaks.size() < order)
	{
		LogDebug("Requested PAM-%zu but only found %zu peaks, cannot proceed\n", order, peaks.size());
		SetData(nullptr, 0);
		return;
	}
	for(size_t i=0; i<order; i++)
		levels.push_back((peaks[i].m_x * binsize) + vmin);

	//Now sort the levels by voltage to get symbol values from lowest to highest
	std::sort(levels.begin(), levels.end());

	//Save levels
	for(size_t i=0; i<levels.size(); i++)
	{
		auto pname = string("Level ") + to_string(i);
		if(m_parameters.find(pname) == m_parameters.end())
			m_parameters[pname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
		m_parameters[pname].SetFloatVal(levels[i]);

		LogTrace("Final level %zu = %f\n", i, levels[i]);
	}
}
