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
#include "PAMEdgeDetectorFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PAMEdgeDetectorFilter::PAMEdgeDetectorFilter(const string& color)
	: Filter(color, CAT_CLOCK)
	, m_order("PAM Order")
	, m_baudname("Symbol rate")
{
	AddDigitalStream("data");
	CreateInput("din");

	m_parameters[m_order] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_order].SetIntVal(3);

	m_parameters[m_baudname] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_HZ));
	m_parameters[m_baudname].SetIntVal(1250000000);	//1.25 Gbps
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool PAMEdgeDetectorFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void PAMEdgeDetectorFilter::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	if(!VerifyAllInputsOK())
	{
		SetData(nullptr, 0);
		return;
	}

	//Get the input data
	auto din = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(0));
	if(!din)
	{
		SetData(nullptr, 0);
		return;
	}
	auto len = din->size();

	int64_t ui = round(FS_PER_SECOND / m_parameters[m_baudname].GetIntVal());

	//Find min/max of the input
	size_t order = m_parameters[m_order].GetIntVal();
	din->PrepareForCpuAccess();
	auto vmin = GetMinVoltage(din);
	auto vmax = GetMaxVoltage(din);
	Unit yunit(Unit::UNIT_VOLTS);
	LogTrace("Bounds are %s to %s\n", yunit.PrettyPrint(vmin).c_str(), yunit.PrettyPrint(vmax).c_str());

	//Take a histogram and find the top N peaks (should be roughly evenly distributed)
	const int64_t nbins = 250;
	auto hist = MakeHistogram(din, vmin, vmax, nbins);
	float binsize = (vmax - vmin) / nbins;

	//Search radius for bins (for now hard code, TODO make this adaptive?)
	const int64_t searchrad = 10;
	ssize_t nend = len - 1;
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

	//Print out level of each symbol by name
	//TODO: naming only tested for PAM3
	LogTrace("Symbol levels:\n");
	bool oddOrder = ((order & 1) == 1);
	int64_t symbase = 0;
	if(oddOrder)
		symbase = -static_cast<int64_t>(order/2);
	for(size_t i=0; i<order; i++)
	{
		LogIndenter li;
		LogTrace("%2" PRIi64 ": %s\n",
			static_cast<int64_t>(i) + symbase,
			yunit.PrettyPrint(levels[i]).c_str());
	}

	//Decision thresholds for initial symbol assignment
	LogTrace("Static thresholds:\n");
	vector<float> sthresholds;
	sthresholds.resize(order-1);
	for(size_t i=0; i<order-1; i++)
	{
		float from = levels[i];
		float to = levels[i+1];
		sthresholds[i] = (from + to) / 2;
	}

	//Midpoints for interpolating each transition
	LogTrace("Transition thresholds:\n");
	vector< vector<float> > lerpthresholds;
	lerpthresholds.resize(order);
	for(size_t i=0; i<order; i++)
	{
		LogIndenter li;

		lerpthresholds[i].resize(order);

		float vfrom = levels[i];
		for(size_t j=0; j<order; j++)
		{
			float vto = levels[j];
			if(i != j)
			{
				float thresh = vfrom + (vto - vfrom)/2;
				lerpthresholds[i][j] = thresh;
				LogTrace("%2" PRIi64 " -> %2" PRIi64 ": %s\n",
					static_cast<int64_t>(i) + symbase,
					static_cast<int64_t>(j) + symbase,
					yunit.PrettyPrint(thresh).c_str());
			}
		}
	}

	//Output waveform is sparse since we interpolate edge positions
	auto cap = SetupEmptySparseDigitalOutputWaveform(din, 0);
	cap->PrepareForCpuAccess();
	cap->m_timescale = 1;

	//Figure out symbol value at time zero
	auto lastSymbol = GetState(din->m_samples[0], sthresholds);
	int64_t tlast = 0;
	size_t ilast = 0;
	LogTrace("First symbol has value %zu (%s)\n", lastSymbol, yunit.PrettyPrint(din->m_samples[0]).c_str());

	//Add initial dummy sample at time zero
	cap->m_offsets.push_back(0);
	cap->m_durations.push_back(1);
	cap->m_samples.push_back(0);

	//Loop over samples and look for transitions
	int64_t minTransitionTime = ui * 0.6;
	LogTrace("minTransitionTime = %s\n", Unit(Unit::UNIT_FS).PrettyPrint(minTransitionTime).c_str());
	bool nextValue = true;
	float noEdgeThreshold = (levels[1] - levels[0]) * 0.1;
	float fracUI = din->m_timescale * 1.0 / ui;
	for(size_t i=1; i<len-1; i++)
	{
		//See what state we're in
		auto vnow = din->m_samples[i];
		auto state = GetState(vnow, sthresholds);
		int64_t tnow = i*din->m_timescale;

		//Look at the delta from the previous value to the next
		//If we're changing rapidly, we're probably still in an edge
		float dv = din->m_samples[i+1] - din->m_samples[i-1];
		float slew = dv / (2*fracUI);
		/*LogTrace("[%s] state=%zu slew=%f\n",
			Unit(Unit::UNIT_FS).PrettyPrint(tnow).c_str(),
			state,
			slew);
		LogIndenter li;*/
		if(fabs(slew) > 0.25)
		{
			//LogTrace("slew reject\n");
			continue;
		}

		//Not an edge if we're the same symbol value we've been before
		//BUT: if we're within a fairly small delta (10% for now) of the nominal symbol value
		//then we're in a multi bit run, don't count this as the start of a transition
		if(state == lastSymbol)
		{
			auto nominal = levels[state];
			auto delta = fabs(vnow - nominal);
			if(delta < noEdgeThreshold)
			{
				/*LogTrace("[%s] lastSymbol=%zu, state=%zu, delta=%.3f, noEdgeThreshold=%.3f\n",
					Unit(Unit::UNIT_FS).PrettyPrint(tnow).c_str(),
					lastSymbol,
					state,
					delta,
					noEdgeThreshold);*/

				ilast = i;
				tlast = tnow;
			}
			//LogTrace("unchanged reject\n");
			continue;
		}

		//State is different. How long has it been?
		//Should have been most of a UI since the last edge
		int64_t delta = tnow - tlast;
		if(delta < minTransitionTime)
		{
			//LogTrace("ui reject\n");
			continue;
		}

		bool rising = state > lastSymbol;
		float target = lerpthresholds[lastSymbol][state];
		//LogTrace("New symbol has value %zu (at time %s)\n", state, Unit(Unit::UNIT_FS).PrettyPrint(tnow).c_str());
		//LogTrace("Searching for level crossing at %s\n", yunit.PrettyPrint(target).c_str());

		//Search the transition region to find the actual level crossing, knowing the start and end states
		size_t j = ilast;
		for(; j < i; j++)
		{
			float a = din->m_samples[j];
			float b = din->m_samples[j+1];

			if(rising && (a <= target) && (b >= target) )
			{
				/*LogTrace("Found rising edge (between samples at %s and %s with values %s and %s)\n",
					Unit(Unit::UNIT_FS).PrettyPrint(j*din->m_timescale).c_str(),
					Unit(Unit::UNIT_FS).PrettyPrint( (j+1)*din->m_timescale).c_str(),
					yunit.PrettyPrint(a).c_str(),
					yunit.PrettyPrint(b).c_str());*/
				break;
			}

			if(!rising && (a >= target) && (b <= target) )
			{
				/*LogTrace("Found falling edge (between samples at %s and %s with values %s and %s)\n",
					Unit(Unit::UNIT_FS).PrettyPrint(j*din->m_timescale).c_str(),
					Unit(Unit::UNIT_FS).PrettyPrint( (j+1)*din->m_timescale).c_str(),
					yunit.PrettyPrint(a).c_str(),
					yunit.PrettyPrint(b).c_str());*/
				break;
			}
		}
		auto frac = InterpolateTime(din, j, target);
		int64_t lerp = j*din->m_timescale + frac*din->m_timescale;

		//Extend previous sample, if any
		size_t outlen = cap->m_offsets.size();
		if(outlen)
			cap->m_durations[outlen-1] = lerp - cap->m_offsets[outlen-1];

		//Add new sample
		cap->m_offsets.push_back(lerp);
		cap->m_durations.push_back(1);
		cap->m_samples.push_back(nextValue);

		//Prepare for the next sample
		nextValue = !nextValue;
		lastSymbol = state;
		ilast = i;
		tlast = lerp;

		/*LogTrace("Interpolated zero crossing at %s\n",
			Unit(Unit::UNIT_FS).PrettyPrint(lerp + din->m_triggerPhase).c_str());

		if(outlen > 10)
			break;*/
	}

	//Done
	cap->MarkModifiedFromCpu();
}

/**
	@brief Figure out the symbol value for a given voltage
 */
size_t PAMEdgeDetectorFilter::GetState(float v, vector<float>& sthresholds)
{
	for(size_t i=0; i<sthresholds.size(); i++)
	{
		if(v < sthresholds[i])
			return i;
	}
	return sthresholds.size();
}
