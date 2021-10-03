/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2021 Andrew D. Zonenberg and contributors                                                         *
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
#include "PeakDetectionFilter.h"
#include <immintrin.h>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// PeakDetector

PeakDetector::PeakDetector()
{
}

PeakDetector::~PeakDetector()
{
}

void PeakDetector::FindPeaks(AnalogWaveform* cap, int64_t max_peaks, float search_hz)
{
	size_t nouts = cap->m_samples.size();
	if(max_peaks == 0)
		m_peaks.clear();
	else
	{
		//Get peak search width in bins
		int64_t search_bins = ceil(search_hz / cap->m_timescale);
		int64_t search_rad = search_bins/2;
		search_rad = max(search_rad, (int64_t)1);

		//Find peaks (TODO: can we vectorize/multithread this?)
		//Start at index 1 so we don't waste a marker on the DC peak
		vector<Peak> peaks;
		ssize_t nend = nouts-1;
		size_t minpeak = 10;		//Skip this many bins at left to avoid false positives on the DC peak
		for(ssize_t i=minpeak; i<(ssize_t)nouts; i++)
		{
			//Locate the peak
			ssize_t left = max((ssize_t)minpeak, (ssize_t)(i - search_rad));
			ssize_t right = min((ssize_t)(i + search_rad), (ssize_t)nend);

			float target = cap->m_samples[i];
			bool is_peak = true;
			for(ssize_t j=left; j<=right; j++)
			{
				if(i == j)
					continue;
				if(cap->m_samples[j] >= target)
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
			left = max((ssize_t)1, i - fine_rad);
			right = min(i + fine_rad, (ssize_t)nouts-1);
			//LogDebug("peak range: %zu, %zu\n", left, right);
			double total = 0;
			double count = 0;
			for(ssize_t j=left; j<=right; j++)
			{
				total += cap->m_samples[j] * cap->m_offsets[j];
				count += cap->m_samples[j];
			}
			ssize_t peak_location = round(total / count);
			//LogDebug("Moved peak from %zu to %zd\n", (size_t)cap->m_offsets[i], peak_location);

			peaks.push_back(Peak(peak_location, target));

			//We know we're the highest point until at least i+search_rad.
			//Don't bother searching those points.
			i += (search_rad-1);
		}

		//Sort the peak table and pluck out the requested count
		sort(peaks.rbegin(), peaks.rend(), less<Peak>());
		m_peaks.clear();
		for(size_t i=0; i<(size_t)max_peaks && i<peaks.size(); i++)
			m_peaks.push_back(peaks[i]);
	}

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PeakDetectionFilter::PeakDetectionFilter(OscilloscopeChannel::ChannelType type, const string& color, Category cat)
	: Filter(type, color, cat)
	, m_numpeaksname("Number of Peaks")
	, m_peakwindowname("Peak Window")
{
	m_parameters[m_numpeaksname] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_numpeaksname].SetIntVal(10);

	m_parameters[m_peakwindowname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ));
	m_parameters[m_peakwindowname].SetFloatVal(500000); //500 kHz between peaks
}

PeakDetectionFilter::~PeakDetectionFilter()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void PeakDetectionFilter::FindPeaks(AnalogWaveform* cap)
{
	PeakDetector::FindPeaks(
		cap,
		m_parameters[m_numpeaksname].GetIntVal(),
		m_parameters[m_peakwindowname].GetFloatVal());
}
