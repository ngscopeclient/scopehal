/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Declaration of PeakDetectionFilter
 */
#ifndef PeakDetectionFilter_h
#define PeakDetectionFilter_h

class Peak
{
public:
	Peak(int64_t x, float y)
		: m_x(x)
		, m_y(y)
	{}

	bool operator<(const Peak& rhs) const
	{ return (m_y < rhs.m_y); }

	int64_t m_x;
	float m_y;
};

class PeakDetector
{
public:
	PeakDetector();
	virtual ~PeakDetector();

	const std::vector<Peak>& GetPeaks()
	{ return m_peaks; }

	template<class T>
	__attribute__((noinline))
	void FindPeaks(T* cap, int64_t max_peaks, float search_hz)
	{
		//input must be analog
		AssertTypeIsAnalogWaveform(cap);

		size_t nouts = cap->m_samples.size();
		if(max_peaks == 0)
			m_peaks.clear();
		else
		{
			//Get peak search width in bins
			int64_t search_bins = ceil(search_hz / cap->m_timescale);
			int64_t search_rad = search_bins/2;
			search_rad = std::max(search_rad, (int64_t)1);

			//Find peaks (TODO: can we vectorize/multithread this?)
			//Start at index 1 so we don't waste a marker on the DC peak
			std::vector<Peak> peaks;
			ssize_t nend = nouts-1;
			size_t minpeak = 10;		//Skip this many bins at left to avoid false positives on the DC peak
			for(ssize_t i=minpeak; i<(ssize_t)nouts; i++)
			{
				//Locate the peak
				ssize_t left = std::max((ssize_t)minpeak, (ssize_t)(i - search_rad));
				ssize_t right = std::min((ssize_t)(i + search_rad), (ssize_t)nend);

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
				left = std::max((ssize_t)1, i - fine_rad);
				right = std::min(i + fine_rad, (ssize_t)nouts-1);
				//LogDebug("peak range: %zu, %zu\n", left, right);
				double total = 0;
				double count = 0;
				for(ssize_t j=left; j<=right; j++)
				{
					total += GetSampleTimesIndex(cap, j);
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
			std::sort(peaks.rbegin(), peaks.rend(), std::less<Peak>());
			m_peaks.clear();
			for(size_t i=0; i<(size_t)max_peaks && i<peaks.size(); i++)
				m_peaks.push_back(peaks[i]);
		}
	}

protected:
	std::vector<Peak> m_peaks;
};

/**
	@brief A filter that does peak detection
 */
class PeakDetectionFilter
	: public Filter
	, public PeakDetector
{
public:
	PeakDetectionFilter(const std::string& color, Category cat);
	virtual ~PeakDetectionFilter();

protected:

	template<class T>
	void FindPeaks(T* cap)
	{
		PeakDetector::FindPeaks(
			cap,
			m_parameters[m_numpeaksname].GetIntVal(),
			m_parameters[m_peakwindowname].GetFloatVal());
	}

	std::string m_numpeaksname;
	std::string m_peakwindowname;
};

#endif

