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
#include "../scopehal/AlignedAllocator.h"
#include "JitterSpectrumFilter.h"

#include <cinttypes>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

JitterSpectrumFilter::JitterSpectrumFilter(const string& color)
	: FFTFilter(color)
{
	m_xAxisUnit = Unit(Unit::UNIT_HZ);
	SetYAxisUnits(Unit(Unit::UNIT_FS), 0);
	m_category = CAT_ANALYSIS;
}

JitterSpectrumFilter::~JitterSpectrumFilter()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool JitterSpectrumFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) &&
		(stream.GetType() == Stream::STREAM_TYPE_ANALOG) &&
		(stream.GetYAxisUnits() == Unit::UNIT_FS)
		)
	{
		return true;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string JitterSpectrumFilter::GetProtocolName()
{
	return "Jitter Spectrum";
}

Filter::DataLocation JitterSpectrumFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

size_t JitterSpectrumFilter::EstimateUIWidth(SparseAnalogWaveform* din)
{
	//Make a histogram of sample durations.
	//Sample no more than 5K UIs since this is just a rough estimate.
	size_t inlen = din->m_samples.size();
	inlen = min(inlen, (size_t)5000);
	map<int64_t, size_t> durations;
	int64_t maxdur = 0;
	for(size_t i=0; i<inlen; i++)
	{
		int64_t dur = din->m_durations[i] / 1000;	//convert to ps, we dont need stupidly high resolution here
													//and this makes the histogram much smaller
		maxdur = max(dur, maxdur);
		if(dur > 0)
			durations[dur] ++;
	}

	//Find peaks in the histogram.
	//These should occur at integer multiples of the unit interval.
	vector<int64_t> peaks;
	for(auto it : durations)
	{
		//See if this is a peak
		int64_t dur = it.first;
		int64_t leftbound = dur * 90 / 100;
		int64_t rightbound = dur * 110 / 100;
		size_t target = it.second;

		bool peak = true;
		for(int64_t i=leftbound; i<=rightbound; i++)
		{
			auto jt = durations.find(i);
			if(jt == durations.end())
				continue;
			if(jt->second > target)
				peak = false;
		}
		if(peak)
			peaks.push_back(dur);
	}

	//The lowest peak that's still reasonably tall is our estimated UI.
	//This doesn't need to be super precise yet (up to 20% error should be pretty harmless).
	//At this point, we just need an approximate threshold for determining how many UIs apart two edges are.
	size_t max_height = 0;
	for(auto dur : peaks)
		max_height = max(max_height, durations[dur]);
	int64_t ui_width = 0;
	size_t threshold_height = max_height / 10;
	for(auto dur : peaks)
	{
		if(durations[dur] > threshold_height)
		{
			ui_width = dur;
			break;
		}
	}

	LogTrace("Initial UI width estimate: %" PRId64 "\n", ui_width);

	//Take a weighted average to smooth out the peak location somewhat.
	int64_t leftbound = ui_width * 90 / 100;
	int64_t rightbound = ui_width * 110 / 100;
	size_t ui_width_samples = 0;
	ui_width = 0;
	for(int64_t i=leftbound; i<=rightbound; i++)
	{
		auto jt = durations.find(i);
		if(jt == durations.end())
			continue;
		ui_width_samples += jt->second;
		ui_width += jt->first * jt->second;
	}
	ui_width /= ui_width_samples;
	LogTrace("Averaged UI width estimate: %" PRId64 "\n", ui_width);

	ui_width *= 1000;	//convert back to fs
	return ui_width;
}

void JitterSpectrumFilter::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndSparseAnalog())
	{
		SetData(NULL, 0);
		return;
	}
	auto din = dynamic_cast<SparseAnalogWaveform*>(GetInput(0).GetData());
	din->PrepareForCpuAccess();

	//Get an initial estimate of the UI width for the waveform
	size_t inlen = din->size();
	size_t ui_width = EstimateUIWidth(din);

	//Loop over the input and copy samples.
	//If we have runs of identical bits, extend the same jitter value.
	//TODO: interpolate?
	AcceleratorBuffer<float> extended_samples;
	extended_samples.reserve(inlen);
	for(size_t i=0; i<inlen; i++)
	{
		int64_t nui = round(1.0 * din->m_durations[i] / ui_width);
		for(int64_t j=0; j<nui; j++)
			extended_samples.push_back(din->m_samples[i]);
	}

	//Refine our estimate of the final UI width.
	//This needs to be fairly precise as the timebase for converting FFT bins to frequency is derived from it.
	size_t capture_duration = din->m_offsets[inlen-1] + din->m_durations[inlen-1];
	size_t num_uis = extended_samples.size();
	double ui_width_final = static_cast<double>(capture_duration) / num_uis;
	LogTrace("Capture is %zu UIs, %s\n", num_uis, Unit(Unit::UNIT_FS).PrettyPrint(capture_duration).c_str());
	LogTrace("Final UI width estimate: %s\n", Unit(Unit::UNIT_FS).PrettyPrint(ui_width_final).c_str());

	//Round size up to next power of two
	const size_t npoints_raw = extended_samples.size();
	const size_t npoints = next_pow2(npoints_raw);
	LogTrace("JitterSpectrumFilter: processing %zu raw points\n", npoints_raw);
	LogTrace("Rounded to %zu\n", npoints);

	//Reallocate buffers if size has changed
	const size_t nouts = npoints/2 + 1;
	if(m_cachedNumPoints != npoints_raw)
		ReallocateBuffers(npoints_raw, npoints, nouts);

	//and do the actual FFT processing
	DoRefresh(din, extended_samples, ui_width_final, npoints, nouts, false, cmdBuf, queue);
}
