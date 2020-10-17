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
	if(max_peaks > 0)
	{
		//Get peak search width in bins
		int64_t search_bins = ceil(search_hz / cap->m_timescale);
		search_bins = min(search_bins, (int64_t)512);	//TODO: reasonable limit
		int64_t search_rad = search_bins/2;

		//Find peaks (TODO: can we vectorize/multithread this?)
		//Start at index 1 so we don't waste a marker on the DC peak
		vector<Peak> peaks;
		for(ssize_t i=1; i<(ssize_t)nouts; i++)
		{
			ssize_t max_delta = 0;
			float max_value = -FLT_MAX;

			for(ssize_t delta = -search_rad; delta <= search_rad; delta ++)
			{
				ssize_t index = i+delta ;
				if( (index < 0) || (index >= (ssize_t)nouts) )
					continue;

				float amp = cap->m_samples[index];
				if(amp > max_value)
				{
					max_value = amp;
					max_delta = delta;
				}
			}

			//If the highest point in the search window is at our location, we're a peak
			if(max_delta == 0)
				peaks.push_back(Peak(cap->m_offsets[i], max_value));
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
