/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2021 Andrew D. Zonenberg                                                                          *
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
	@brief Declaration of Waveform
 */

#ifndef Waveform_h
#define Waveform_h

#include <vector>
#include <AlignedAllocator.h>

/**
	@brief Wrapper around a primitive data type that has an empty default constructor.

	Can be seamlessly casted to that type. This allows STL data structures to be created with explicitly uninitialized
	members via resize() and avoids a nasty memset that wastes a lot of time.
*/
template<class T>
class EmptyConstructorWrapper
{
public:
	EmptyConstructorWrapper()
	{}

	EmptyConstructorWrapper(const T& rhs)
	: m_value(rhs)
	{}

	operator T&()
	{ return m_value; }

	T& operator=(const T& rhs)
	{
		m_value = rhs;
		return *this;
	}

	T m_value;
};

/**
	@brief Base class for all Waveform specializations

	One waveform contains a time-series of sample objects as well as scale information etc. The samples may
	or may not be at regular intervals depending on whether the Oscilloscope uses RLE compression.

	The WaveformBase contains all metadata, but the actual samples are stored in a derived class member.
 */
class WaveformBase
{
public:
	WaveformBase()
		: m_timescale(0)
		, m_startTimestamp(0)
		, m_startFemtoseconds(0)
		, m_triggerPhase(0)
		, m_densePacked(false)
	{}

	//empty virtual destructor in case any derived classes need one
	virtual ~WaveformBase()
	{}

	/**
		@brief The time scale, in femtoseconds per timestep, used by this channel.

		This is used as a scaling factor for individual sample time values as well as to compute the maximum zoom value
		for the time axis.
	 */
	int64_t m_timescale;

	///@brief Start time of the acquisition, rounded to nearest second
	time_t	m_startTimestamp;

	///@brief Fractional start time of the acquisition (femtoseconds since m_startTimestamp)
	int64_t m_startFemtoseconds;

	/**
		@brief Offset, in femtoseconds, from the trigger to the sampling clock.

		This is most commonly the output of a time-to-digital converter and ranges from 0 to 1 sample, but this
		should NOT be assumed to be the case in all waveforms.

		LeCroy oscilloscopes, for example, can have negative trigger phases of 150ns or more on digital channels
		since the digital waveform can start significantly before the analog waveform!
	 */
	int64_t m_triggerPhase;

	/**
		@brief True if the waveform is "dense packed".

		This means that m_durations is always 1, and m_offsets ranges from 0 to m_offsets.size()-1.

		If dense packed, we can often perform various optimizations to avoid excessive copying of waveform data.

		Most oscilloscopes output dense packed waveforms natively.
	 */
	bool m_densePacked;

	///@brief Start timestamps of each sample
	std::vector<
		EmptyConstructorWrapper<int64_t>,
		AlignedAllocator< EmptyConstructorWrapper<int64_t>, 64 >
		> m_offsets;

	///@brief Durations of each sample
	std::vector<
		EmptyConstructorWrapper<int64_t>,
		AlignedAllocator< EmptyConstructorWrapper<int64_t>, 64 >
		> m_durations;

	virtual void clear()
	{
		m_offsets.clear();
		m_durations.clear();
	}

	virtual void Resize(size_t size)
	{
		m_offsets.resize(size);
		m_durations.resize(size);
	}

	/**
		@brief Copies offsets/durations from one waveform to another.

		Must have been resized to match rhs first.
	 */
	void CopyTimestamps(const WaveformBase* rhs)
	{
		size_t len = sizeof(int64_t) * rhs->m_offsets.size();
		memcpy((void*)&m_offsets[0], (void*)&rhs->m_offsets[0], len);
		memcpy((void*)&m_durations[0], (void*)&rhs->m_durations[0], len);
	}
};

/**
	@brief A waveform that contains actual data
 */
template<class S>
class Waveform : public WaveformBase
{
public:

	///@brief Sample data
	std::vector< S, AlignedAllocator<S, 64> > m_samples;

	virtual void Resize(size_t size)
	{
		m_offsets.resize(size);
		m_durations.resize(size);
		m_samples.resize(size);
	}

	virtual void clear()
	{
		m_offsets.clear();
		m_durations.clear();
		m_samples.clear();
	}
};

typedef Waveform<EmptyConstructorWrapper<bool> >	DigitalWaveform;
typedef Waveform<EmptyConstructorWrapper<float>>	AnalogWaveform;

typedef Waveform< std::vector<bool> > 	DigitalBusWaveform;
typedef Waveform<char>					AsciiWaveform;

#endif
