/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2020 Andrew D. Zonenberg                                                                          *
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
#include <immintrin.h>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

JitterSpectrumFilter::JitterSpectrumFilter(const string& color)
	: FFTFilter(color)
{
	m_xAxisUnit = Unit(Unit::UNIT_HZ);
	m_yAxisUnit = Unit(Unit::UNIT_FS);
	m_category = CAT_ANALYSIS;

	m_range = 1;
	m_offset = -0.5;
}

JitterSpectrumFilter::~JitterSpectrumFilter()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string JitterSpectrumFilter::GetProtocolName()
{
	return "Jitter Spectrum";
}

void JitterSpectrumFilter::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "JitterSpectrum(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

size_t JitterSpectrumFilter::EstimateUIWidth(AnalogWaveform* din)
{
	//Make a histogram of sample durations
	size_t inlen = din->m_samples.size();
	map<int64_t, size_t> durations;
	int64_t maxdur = 0;
	for(size_t i=0; i<inlen; i++)
	{
		int64_t dur = din->m_durations[i];
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
	LogTrace("Initial UI width estimate: %zu\n", ui_width);

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
	LogTrace("Averaged UI width estimate: %zu\n", ui_width);

	return ui_width;
}

void JitterSpectrumFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}
	auto din = GetAnalogInputWaveform(0);

	//Get an initial estimate of the UI width for the waveform
	size_t inlen = din->m_samples.size();
	size_t ui_width = EstimateUIWidth(din);

	//Loop over the input and copy samples.
	//If we have runs of identical bits, extend the same jitter value.
	//TODO: interpolate?
	vector<float, AlignedAllocator<float, 64>> extended_samples;
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
	LogTrace("Final UI width estimate: %.1f\n", ui_width_final);

	//Round size up to next power of two
	const size_t npoints_raw = extended_samples.size();
	const size_t npoints = pow(2, ceil(log2(npoints_raw)));
	LogTrace("JitterSpectrumFilter: processing %zu raw points\n", npoints_raw);
	LogTrace("Rounded to %zu\n", npoints);

	//Reallocate buffers if size has changed
	const size_t nouts = npoints/2 + 1;
	if(m_cachedNumPoints != npoints_raw)
		ReallocateBuffers(npoints_raw, npoints, nouts);

	//Copy the input with windowing, then zero pad to the desired input length
	ApplyWindow(
		&extended_samples[0],
		npoints_raw,
		m_rdin,
		static_cast<WindowFunction>(m_parameters[m_windowName].GetIntVal()));
	memset(m_rdin + npoints_raw, 0, (npoints - npoints_raw) * sizeof(float));

	//and do the actual FFT processing
	DoRefresh(din, ui_width_final, npoints, nouts, false);
}
