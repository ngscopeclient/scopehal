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
	@brief Declaration of Waveform
 */

#ifndef Waveform_h
#define Waveform_h

#include <vector>
#include <AlignedAllocator.h>

#include "StandardColors.h"
#include "AcceleratorBuffer.h"

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
		, m_flags(0)
		, m_revision(0)
	{
	}

	WaveformBase(const WaveformBase& rhs)
		: m_timescale(rhs.m_timescale)
		, m_startTimestamp(rhs.m_startTimestamp)
		, m_startFemtoseconds(rhs.m_startFemtoseconds)
		, m_triggerPhase(rhs.m_triggerPhase)
		, m_flags(rhs.m_flags)
		, m_revision(rhs.m_revision)
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
		@brief Flags that apply to this waveform. Bitfield.

		WAVEFORM_CLIPPING: Scope indicated that this waveform is clipped.
	 */
	uint8_t m_flags;

	/**
		@brief Revision number

		This is a monotonically increasing counter that indicates waveform data has changed. Filters may choose to
		cache pre-processed versions of input data (for example, resampled versions of raw input) as long as the
		pointer and revision number have not changed.
	 */
	uint64_t m_revision;

	enum
	{
		WAVEFORM_CLIPPING = 1
	};

	virtual void clear() =0;
	virtual void Resize(size_t size) =0;

	virtual size_t size() const  =0;

	virtual bool empty()
	{ return size() == 0; }

	virtual std::string GetText(size_t /*i*/)
	{
		return "(unimplemented)";
	}

	virtual Gdk::Color GetColor(size_t /*i*/)
	{
		return StandardColors::colors[StandardColors::COLOR_ERROR];
	}

	virtual void PrepareForCpuAccess() =0;
	virtual void PrepareForGpuAccess() =0;
	virtual void MarkSamplesModifiedFromCpu() =0;
	virtual void MarkSamplesModifiedFromGpu() =0;

	virtual void MarkModifiedFromCpu() =0;
	virtual void MarkModifiedFromGpu() =0;
};

template<class S> class SparseWaveform;

class SparseWaveformBase : public WaveformBase
{
public:

	SparseWaveformBase()
	{
		//Default timestamps to CPU/GPU mirror
		m_offsets.SetCpuAccessHint(AcceleratorBuffer<int64_t>::HINT_LIKELY);
		m_offsets.SetGpuAccessHint(AcceleratorBuffer<int64_t>::HINT_LIKELY);

		m_durations.SetCpuAccessHint(AcceleratorBuffer<int64_t>::HINT_LIKELY);
		m_durations.SetGpuAccessHint(AcceleratorBuffer<int64_t>::HINT_LIKELY);

		m_offsets.PrepareForCpuAccess();
		m_durations.PrepareForCpuAccess();
	}

	virtual ~SparseWaveformBase()
	{}

	///@brief Start timestamps of each sample
	AcceleratorBuffer<int64_t> m_offsets;

	///@brief Durations of each sample
	AcceleratorBuffer<int64_t> m_durations;

	/**
		@brief Copies offsets/durations from one waveform to another.
	 */
	void CopyTimestamps(const SparseWaveformBase* rhs)
	{
		m_offsets.CopyFrom(rhs->m_offsets);
		m_durations.CopyFrom(rhs->m_durations);
	}

	void MarkTimestampsModifiedFromCpu()
	{
		m_offsets.MarkModifiedFromCpu();
		m_durations.MarkModifiedFromCpu();
	}

	void MarkTimestampsModifiedFromGpu()
	{
		m_offsets.MarkModifiedFromGpu();
		m_durations.MarkModifiedFromGpu();
	}

	virtual void MarkModifiedFromCpu()
	{
		MarkSamplesModifiedFromCpu();
		MarkTimestampsModifiedFromCpu();
	}

	virtual void MarkModifiedFromGpu()
	{
		MarkSamplesModifiedFromGpu();
		MarkTimestampsModifiedFromGpu();
	}
};

class UniformWaveformBase : public WaveformBase
{
public:
	UniformWaveformBase()
	{}

	/**
		@brief Creates a uniform waveform as a copy of a sparse one
	 */
	UniformWaveformBase(const SparseWaveformBase& rhs)
		: WaveformBase(rhs)
	{}

	virtual ~UniformWaveformBase()
	{}
};

/**
	@brief A waveform sampled at uniform intervals
 */
template<class S>
class UniformWaveform : public UniformWaveformBase
{
public:

	UniformWaveform()
	{}

	/**
		@brief Creates a uniform waveform as a copy of a sparse one which happens to be sampled at uniform rate.

		No resampling or validation of sample intervals/durations is performed.
	 */
	UniformWaveform(const SparseWaveform<S>& rhs)
		: UniformWaveformBase(rhs)
	{
		m_samples.CopyFrom(rhs.m_samples);
	}

	virtual ~UniformWaveform()
	{}

	///@brief Sample data
	AcceleratorBuffer<S> m_samples;

	virtual void Resize(size_t size)
	{ m_samples.resize(size); }

	virtual size_t size() const
	{ return m_samples.size(); }

	virtual void clear()
	{ m_samples.clear(); }

	virtual void PrepareForCpuAccess()
	{ m_samples.PrepareForCpuAccess(); }

	virtual void PrepareForGpuAccess()
	{ m_samples.PrepareForGpuAccess(); }

	virtual void MarkSamplesModifiedFromCpu()
	{ m_samples.MarkModifiedFromCpu(); }

	virtual void MarkSamplesModifiedFromGpu()
	{ m_samples.MarkModifiedFromGpu(); }

	void MarkModifiedFromCpu()
	{ MarkSamplesModifiedFromCpu(); }

	void MarkModifiedFromGpu()
	{ MarkSamplesModifiedFromGpu(); }

	void SetGpuAccessHint(enum AcceleratorBuffer<S>::UsageHint hint)
	{ m_samples.SetGpuAccessHint(hint); }
};

/**
	@brief A waveform sampled at irregular intervals
 */
template<class S>
class SparseWaveform : public SparseWaveformBase
{
public:

	SparseWaveform()
	{
		//Default data to CPU/GPU mirror
		m_samples.SetCpuAccessHint(AcceleratorBuffer<S>::HINT_LIKELY);
		m_samples.SetGpuAccessHint(AcceleratorBuffer<S>::HINT_LIKELY);
		m_samples.PrepareForCpuAccess();
	}

	/**
		@brief Constructs a sparse waveform as a copy of a uniform waveform
	 */
	SparseWaveform(UniformWaveform<S>& rhs)
	{
		m_samples.SetCpuAccessHint(AcceleratorBuffer<S>::HINT_LIKELY);
		m_samples.SetGpuAccessHint(AcceleratorBuffer<S>::HINT_LIKELY);
		m_samples.PrepareForCpuAccess();

		//Copy sample data
		Resize(rhs.size());
		m_samples.CopyFrom(rhs.m_samples);

		//Generate offset/duration values
		//TODO: is it worth vectorizing this since this is mostly meant to be a compatibility layer?
		for(size_t i=0; i<m_offsets.size(); i++)
		{
			m_offsets[i] = i;
			m_durations[i] = 1;
		}
	}

	virtual ~SparseWaveform()
	{}

	///@brief Sample data
	AcceleratorBuffer<S> m_samples;

	virtual void Resize(size_t size)
	{
		m_offsets.resize(size);
		m_durations.resize(size);
		m_samples.resize(size);
	}

	virtual size_t size() const
	{ return m_samples.size(); }

	virtual void clear()
	{
		m_offsets.clear();
		m_durations.clear();
		m_samples.clear();
	}

	virtual void PrepareForCpuAccess()
	{
		m_offsets.PrepareForCpuAccess();
		m_durations.PrepareForCpuAccess();
		m_samples.PrepareForCpuAccess();
	}

	virtual void PrepareForGpuAccess()
	{
		m_offsets.PrepareForGpuAccess();
		m_durations.PrepareForGpuAccess();
		m_samples.PrepareForGpuAccess();
	}

	virtual void MarkSamplesModifiedFromCpu()
	{ m_samples.MarkModifiedFromCpu(); }

	virtual void MarkSamplesModifiedFromGpu()
	{ m_samples.MarkModifiedFromGpu(); }

	void SetGpuAccessHint(enum AcceleratorBuffer<S>::UsageHint hint)
	{
		m_offsets.SetGpuAccessHint(static_cast<AcceleratorBuffer<int64_t>::UsageHint>(hint));
		m_durations.SetGpuAccessHint(static_cast<AcceleratorBuffer<int64_t>::UsageHint>(hint));
		m_samples.SetGpuAccessHint(hint);
	}
};

typedef SparseWaveform<bool> 					SparseDigitalWaveform;
typedef UniformWaveform<bool>					UniformDigitalWaveform;
typedef SparseWaveform<float>					SparseAnalogWaveform;
typedef UniformWaveform<float>					UniformAnalogWaveform;
typedef SparseWaveform< std::vector<bool> > 	SparseDigitalBusWaveform;

//Make sure inline helpers aren't warned about if unused
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

//Helper methods for identifying what a waveform is at compile time
static bool IsWaveformUniform(const SparseWaveformBase* /*unused*/);
static bool IsWaveformUniform(const UniformWaveformBase* /*unused*/);
static int64_t GetOffset(const SparseWaveformBase* wfm, size_t i);
static int64_t GetOffset(const UniformWaveformBase* /*wfm*/, size_t i);
static int64_t GetDuration(const SparseWaveformBase* wfm, size_t i);
static int64_t GetDuration(const UniformWaveformBase* /*wfm*/, size_t /*i*/);

bool IsWaveformUniform(const SparseWaveformBase* /*unused*/)
{ return false; }

bool IsWaveformUniform(const UniformWaveformBase* /*unused*/)
{ return true; }

//Helper methods for weighted averaging of samples
static float GetSampleTimesIndex(const UniformAnalogWaveform* wfm, ssize_t i);
static float GetSampleTimesIndex(const SparseAnalogWaveform* wfm, ssize_t i);

float GetSampleTimesIndex(const UniformAnalogWaveform* wfm, ssize_t i)
{ return wfm->m_samples[i] * i; }

float GetSampleTimesIndex(const SparseAnalogWaveform* wfm, ssize_t i)
{ return wfm->m_samples[i] * wfm->m_offsets[i]; }

//Helper methods for getting timestamps of a waveform
int64_t GetOffset(const SparseWaveformBase* wfm, size_t i)
{ return wfm->m_offsets[i]; }

int64_t GetOffset(const UniformWaveformBase* /*wfm*/, size_t i)
{ return i; }

int64_t GetDuration(const SparseWaveformBase* wfm, size_t i)
{ return wfm->m_durations[i]; }

int64_t GetDuration(const UniformWaveformBase* /*wfm*/, size_t /*i*/)
{ return 1; }

template<class T>
int64_t GetOffsetScaled(T* wfm, size_t i)
{ return (GetOffset(wfm, i) * wfm->m_timescale) + wfm->m_triggerPhase; }

template<class T>
int64_t GetDurationScaled(T* wfm, size_t i)
{ return GetDuration(wfm, i) * wfm->m_timescale; }

/**
	@brief Helper for calling GetOffset() on a waveform which may be sparse or uniform

	The caller is expected to dynamic_cast the waveform twice and pass both copies, one of which shoule be null.
 */
static int64_t GetOffset(const SparseWaveformBase* sparse, const UniformWaveformBase* uniform, size_t i);

/**
	@brief Helper for calling GetDuration() on a waveform which may be sparse or uniform

	The caller is expected to dynamic_cast the waveform twice and pass both copies, one of which shoule be null.
 */
static int64_t GetDuration(const SparseWaveformBase* sparse, const UniformWaveformBase* uniform, size_t i);

/**
	@brief Helper for calling GetOffsetScaled() on a waveform which may be sparse or uniform

	The caller is expected to dynamic_cast the waveform twice and pass both copies, one of which shoule be null.
 */
static int64_t GetOffsetScaled(const SparseWaveformBase* sparse, const UniformWaveformBase* uniform, size_t i);

/**
	@brief Helper for calling GetDurationScaled() on a waveform which may be sparse or uniform

	The caller is expected to dynamic_cast the waveform twice and pass both copies, one of which shoule be null.
 */
static int64_t GetDurationScaled(const SparseWaveformBase* sparse, const UniformWaveformBase* uniform, size_t i);

int64_t GetOffset(const SparseWaveformBase* sparse, const UniformWaveformBase* uniform, size_t i)
{
	if(sparse)
		return GetOffset(sparse, i);
	else
		return GetOffset(uniform, i);
}

int64_t GetDuration(const SparseWaveformBase* sparse, const UniformWaveformBase* uniform, size_t i)
{
	if(sparse)
		return GetDuration(sparse, i);
	else
		return GetDuration(uniform, i);
}

int64_t GetOffsetScaled(const SparseWaveformBase* sparse, const UniformWaveformBase* uniform, size_t i)
{
	if(sparse)
		return GetOffsetScaled(sparse, i);
	else
		return GetOffsetScaled(uniform, i);
}

int64_t GetDurationScaled(const SparseWaveformBase* sparse, const UniformWaveformBase* uniform, size_t i)
{
	if(sparse)
		return GetDurationScaled(sparse, i);
	else
		return GetDurationScaled(uniform, i);
}

/**
	@brief Helper for getting the value of a waveform which may be sparse or uniform

	The caller is expected to dynamic_cast the waveform twice and pass both copies, one of which should be null.
 */
template<class T>
static T GetValue(const SparseWaveform<T>* sparse, const UniformWaveform<T>* uniform, size_t i)
{
	if(sparse)
		return sparse->m_samples[i];
	else
		return uniform->m_samples[i];
}

//Template helper methods for validating that an input is the correct type
static void AssertTypeIsSparseWaveform(const SparseWaveformBase* /*unused*/);
static void AssertTypeIsUniformWaveform(const UniformWaveformBase* /*unused*/);
static void AssertTypeIsAnalogWaveform(const SparseAnalogWaveform* /*unused*/);
static void AssertTypeIsAnalogWaveform(const UniformAnalogWaveform* /*unused*/);
static void AssertTypeIsDigitalWaveform(const SparseDigitalWaveform* /*unused*/);
static void AssertTypeIsDigitalWaveform(const UniformDigitalWaveform* /*unused*/);

void AssertTypeIsSparseWaveform(const SparseWaveformBase* /*unused*/){}
void AssertTypeIsUniformWaveform(const UniformWaveformBase* /*unused*/){}
void AssertTypeIsAnalogWaveform(const SparseAnalogWaveform* /*unused*/){}
void AssertTypeIsAnalogWaveform(const UniformAnalogWaveform* /*unused*/){}
void AssertTypeIsDigitalWaveform(const SparseDigitalWaveform* /*unused*/){}
void AssertTypeIsDigitalWaveform(const UniformDigitalWaveform* /*unused*/){}

//Template helper methods for validating that two waveforms are the same sample type
//(but either may be sparse or uniform)
template<class T>
void AssertSampleTypesAreSame(const SparseWaveform<T>* /*a*/, const SparseWaveform<T>* /*b*/)
{}

template<class T>
void AssertSampleTypesAreSame(const SparseWaveform<T>* /*a*/, const UniformWaveform<T>* /*b*/)
{}

template<class T>
void AssertSampleTypesAreSame(const UniformWaveform<T>* /*a*/, const SparseWaveform<T>* /*b*/)
{}

template<class T>
void AssertSampleTypesAreSame(const UniformWaveform<T>* /*a*/, const UniformWaveform<T>* /*b*/)
{}

#pragma GCC diagnostic pop

#endif
