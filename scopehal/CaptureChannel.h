/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2017 Andrew D. Zonenberg                                                                          *
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
	@brief Declaration of CaptureChannel
 */

#ifndef CaptureChannel_h
#define CaptureChannel_h

#include "OscilloscopeSample.h"
#include <vector>

/**
	@brief Base class for all CaptureChannel specializations
 */
class CaptureChannelBase
{
public:
	virtual ~CaptureChannelBase()
	{}

	/**
		@brief The time scale, in picoseconds per timestep, used by this channel.

		This is used as a scaking factor for individual sample time values as well as to compute the maximum zoom value
		for the time axis.
	 */
	int64_t m_timescale;

	virtual size_t GetDepth() const =0;

	/**
		@brief Gets the time the capture ends at, in time steps
	 */
	virtual int64_t GetEndTime() const =0;

	virtual int64_t GetSampleStart(size_t i) const =0;
	virtual int64_t GetSampleLen(size_t i) const =0;

	virtual bool EqualityTest(size_t i, size_t j) const =0;

	virtual bool SamplesAdjacent(size_t i, size_t j) const =0;
};

/**
	@brief A single channel of an oscilloscope capture.

	One channel contains a time-series of OscilloscopeSample objects as well as scale information etc. The samples may
	or may not be at regular intervals depending on whether the Oscilloscope uses RLE compression.

	The channel data is independent of the renderer.
 */
template<class S>
class CaptureChannel : public CaptureChannelBase
{
public:

	typedef std::vector< OscilloscopeSample<S> > vtype;

	/**
		@brief The actual samples
	 */
	vtype m_samples;

	virtual size_t GetDepth() const
	{
		return m_samples.size();
	}

	virtual int64_t GetSampleStart(size_t i) const
	{
		return m_samples[i].m_offset;
	}

	virtual int64_t GetSampleLen(size_t i) const
	{
		return m_samples[i].m_duration;
	}

	virtual bool EqualityTest(size_t i, size_t j) const
	{
		return (m_samples[i].m_sample == m_samples[j].m_sample);
	}

	virtual bool SamplesAdjacent(size_t i, size_t j) const
	{
		auto sa = m_samples[i];
		auto sb = m_samples[j];

		return (sa.m_offset + sa.m_duration) == sb.m_offset;
	}

	virtual int64_t GetEndTime() const
	{
		if(m_samples.empty())
			return 0;
		const OscilloscopeSample<S>& samp = m_samples[m_samples.size() - 1];
		return samp.m_offset + samp.m_duration;
	}

	size_t size() const
	{ return m_samples.size(); }

	S& operator[](size_t i)
	{ return m_samples[i]; }

	typename vtype::iterator begin()
	{ return m_samples.begin(); }

	typename vtype::iterator end()
	{ return m_samples.end(); }

};

typedef CaptureChannel<bool> DigitalCapture;
typedef CaptureChannel< std::vector<bool> > DigitalBusCapture;
typedef CaptureChannel<float> AnalogCapture;
typedef CaptureChannel<char> AsciiCapture;
typedef CaptureChannel<unsigned char> ByteCapture;
typedef CaptureChannel<std::string> StringCapture;

#endif
