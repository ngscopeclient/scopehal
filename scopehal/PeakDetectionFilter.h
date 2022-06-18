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

	void FindPeaks(AnalogWaveform* cap, int64_t max_peaks, float search_hz);

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
	void FindPeaks(AnalogWaveform* cap);

	std::string m_numpeaksname;
	std::string m_peakwindowname;
};

#endif

