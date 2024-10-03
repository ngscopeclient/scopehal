/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Declaration of WaveformBase, SparseWaveformBase, UniformWaveformBase
	@ingroup datamodel
 */

#ifndef Waveform_h
#define Waveform_h

#include <vector>
#include <optional>
#include <AlignedAllocator.h>

#include "StandardColors.h"
#include "AcceleratorBuffer.h"

/**
	@brief Base class for all Waveform specializations
	@ingroup datamodel

	One waveform contains a time-series of sample objects as well as scale information etc. The samples may
	or may not be at regular intervals depending on whether the source instrument uses RLE compression, whether
	the data is derived from a math/filter block rather than physical measurements, etc.

	The WaveformBase contains all metadata about the waveform, but the actual samples (and timestamps, if sparse)
	are stored in a derived class member.
 */
class WaveformBase
{
public:

	/**
		@brief Creates an empty waveform
	 */
	WaveformBase()
		: m_timescale(0)
		, m_startTimestamp(0)
		, m_startFemtoseconds(0)
		, m_triggerPhase(0)
		, m_flags(0)
		, m_revision(0)
		, m_cachedColorRevision(0)
	{
	}

	/**
		@brief Creates a waveform, copying metadata from another
	 */
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
		@brief Assings a human readable name to the waveform for debug purposes

		This value may be printed in internal log messages, by the Vulkan validation layers, displayed in frame
		debuggers, etc.
	 */
	virtual void Rename(const std::string& name = "") = 0;

	/**
		@brief The time scale, in X axis units (usually femtoseconds) per timestep, used by this channel.

		This is used as a scaling factor for individual sample time values as well as to compute the maximum zoom value
		for the time axis.
	 */
	int64_t m_timescale;

	///@brief Start time of the acquisition, integer part
	time_t	m_startTimestamp;

	///@brief Start time of the acquisition, fractional part (femtoseconds since since the UTC second)
	int64_t m_startFemtoseconds;

	/**
		@brief Offset, in X axis units (usually femtoseconds), from the trigger to the sampling clock.

		This is most commonly the output of a time-to-digital converter or trigger interpolator and will thus be in
		the range [0, 1] samples, but this should NOT be assumed to be the case in all waveforms.

		LeCroy oscilloscopes, for example, can have negative trigger phases of 150ns or more on digital channels
		since the digital waveform can start significantly before the analog waveform. Secondary scopes of a multi-scope
		trigger group may have very large positive or negative trigger phases as a result of trigger path delay
		calibration or intentional time-shifting of one scope's sampling window relative to that of another.
	 */
	int64_t m_triggerPhase;

	/**
		@brief Flags that apply to this waveform. Bitfield containing zero or more WaveformFlags_t values
	 */
	uint8_t m_flags;

	/**
		@brief Revision number

		This is a monotonically increasing counter that indicates waveform data has changed. Filters may choose to
		cache pre-processed versions of input data (for example, resampled versions of raw input) as long as the
		pointer and revision number have not changed.
	 */
	uint64_t m_revision;

	///@brief Flags which may apply to m_flags
	enum WaveformFlags_t
	{
		///@brief Waveform amplitude exceeded ADC range, values were clipped
		WAVEFORM_CLIPPING = 1
	};

	///@brief Remove all samples from this waveform
	virtual void clear() =0;

	/**
		@brief Reallocates buffers so the waveform contains the specified number of samples.

		If the waveform shrinks, excess memory is freed. If the waveform grows, new samples are uninitialized.

		@param size		New size of the waveform buffer, in samples
	 */
	virtual void Resize(size_t size) =0;

	///@brief Returns the number of samples in this waveform
	virtual size_t size() const  =0;

	///@brief Returns true if this waveform contains no samples, false otherwise
	virtual bool empty()
	{ return size() == 0; }

	/**
		@brief Returns the text representation of a given protocol sample.

		Not used for non-protocol waveforms.

		@param i	Sample index
	 */
	virtual std::string GetText([[maybe_unused]] size_t i)
	{
		return "(unimplemented)";
	}

	/**
		@brief Returns the displayed color (in HTML #rrggbb or #rrggbbaa notation) of a given protocol sample.

		Not used for non-protocol waveforms.

		@param i	Sample index
	 */
	virtual std::string GetColor(size_t /*i*/)
	{
		return StandardColors::colors[StandardColors::COLOR_ERROR];
	}

	/**
		@brief Returns the packed RGBA32 color of a given protocol sample calculated by CacheColors()

		Not used for non-protocol waveforms.

		@param i	Sample index
	 */
	virtual uint32_t GetColorCached(size_t i)
	{ return m_protocolColors[i]; }

	/**
		@brief Indicates that this waveform is going to be used by the CPU in the near future.

		This ensures the CPU-side copy of the data is coherent with the most recently modified (CPU or GPU side) copy.
	 */
	virtual void PrepareForCpuAccess() =0;

	/**
		@brief Indicates that this waveform is going to be used by the CPU in the near future.

		This ensures the GPU-side copy of the data is coherent with the most recently modified (CPU or GPU side) copy.
	 */
	virtual void PrepareForGpuAccess() =0;

	/**
		@brief Indicates that this waveform's sample data has been modified on the CPU and the GPU-side copy is no longer coherent
	 */
	virtual void MarkSamplesModifiedFromCpu() =0;

	/**
		@brief Indicates that this waveform's sample data has been modified on the GPU and the CPU-side copy is no longer coherent
	 */
	virtual void MarkSamplesModifiedFromGpu() =0;

	/**
		@brief Indicates that this waveform's sample data and timestamps have been modified on the CPU and the GPU-side copy is no longer coherent
	 */
	virtual void MarkModifiedFromCpu() =0;

	/**
		@brief Indicates that this waveform's sample data and timestamps have been modified on the GPU and the CPU-side copy is no longer coherent
	 */
	virtual void MarkModifiedFromGpu() =0;

	virtual void CacheColors();

	///@brief Free GPU-side memory if we are short on VRAM or do not anticipate using this waveform for a while
	virtual void FreeGpuMemory() =0;

	///@brief Returns true if we have at least one buffer resident on the GPU
	virtual bool HasGpuBuffer() =0;

protected:

	///@brief Cache of packed RGBA32 data with colors for each protocol decode event. Empty for non-protocol waveforms.
	AcceleratorBuffer<uint32_t> m_protocolColors;

	///@brief Revision we last cached colors of
	uint64_t m_cachedColorRevision;
};

template<class S> class SparseWaveform;

/**
	@brief Base class for waveforms with nonuniform sample rate
	@ingroup datamodel

	Each sample in a sparse waveform has a start time and duration. Samples must be monotonic (each sample begins at or
	after the end of the previous) however gaps between samples are allowed. This is common in the case of e.g.
	protocol decode events where there may be a long interval between the end of one packet or data byte and the start
	of the next.

	This class contains timestamp information but no actual waveform data; SparseWaveform contains the actual data.
 */
class SparseWaveformBase : public WaveformBase
{
public:

	/**
		@brief Constructs a new empty sparse waveform
	 */
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

	///@brief Start timestamps of each sample, in multiples of m_timescale
	AcceleratorBuffer<int64_t> m_offsets;

	///@brief Durations of each sample, in multiples of m_timescale
	AcceleratorBuffer<int64_t> m_durations;

	/**
		@brief Copies offsets/durations from another waveform into this one.

		Commonly used by filters which perform 1:1 transformations on incoming data.

		@param rhs	Source waveform for timestamp data
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

	virtual void MarkModifiedFromCpu() override
	{
		MarkSamplesModifiedFromCpu();
		MarkTimestampsModifiedFromCpu();
	}

	virtual void MarkModifiedFromGpu() override
	{
		MarkSamplesModifiedFromGpu();
		MarkTimestampsModifiedFromGpu();
	}
};

/**
	@brief Base class for waveforms with data sampled at uniform intervals
	@ingroup datamodel
 */
class UniformWaveformBase : public WaveformBase
{
public:
	UniformWaveformBase()
	{}

	/**
		@brief Creates a uniform waveform as a copy of a sparse one.

		It is assumed that the sparse waveform is actually sampled at regular intervals (i.e. m_durations={1, 1, ...1}
		and m_offsets = {0, 1, 2... N} ). If this is not the case, sample data is copied verbatim but timestamps of
		the resulting waveform will be incorrect. No validation of timestamps are performed.
	 */
	UniformWaveformBase(const SparseWaveformBase& rhs)
		: WaveformBase(rhs)
	{}

	virtual ~UniformWaveformBase()
	{}
};

/**
	@brief A waveform sampled at uniform intervals.
	@ingroup datamodel

	This is the most common type of waveform acquired by an oscilloscope, logic analyzer in timing mode, etc.
 */
template<class S>
class UniformWaveform : public UniformWaveformBase
{
public:

	/**
		@brief Creates a new uniform waveform

		@param name Internal name for this waveform, to be displayed in debug log messages etc
	 */
	UniformWaveform(const std::string& name = "")
	{
		Rename(name);

		//Default data to CPU/GPU mirror
		m_samples.SetCpuAccessHint(AcceleratorBuffer<S>::HINT_LIKELY);
		m_samples.SetGpuAccessHint(AcceleratorBuffer<S>::HINT_LIKELY);
		m_samples.PrepareForCpuAccess();
	}

	virtual void Rename(const std::string& name = "") override
	{
		if(name.empty())
			m_samples.SetName(std::string("UniformWaveform<") + typeid(S).name() + ">.m_samples");
		else
			m_samples.SetName(name + ".m_samples");
	}

	/**
		@brief Creates a uniform waveform as a copy of a sparse one which happens to be sampled at uniform rate.

		It is assumed that the sparse waveform is actually sampled at regular intervals (i.e. m_durations={1, 1, ...1}
		and m_offsets = {0, 1, 2... N} ). If this is not the case, sample data is copied verbatim but timestamps of
		the resulting waveform will be incorrect. No validation of timestamps are performed.
	 */
	UniformWaveform(const SparseWaveform<S>& rhs)
		: UniformWaveformBase(rhs)
	{
		m_samples.SetName(std::string("UniformWaveform<") + typeid(S).name() + ">.m_samples");

		m_samples.CopyFrom(rhs.m_samples);
	}

	virtual ~UniformWaveform()
	{}

	///@brief Sample data
	AcceleratorBuffer<S> m_samples;

	virtual void FreeGpuMemory() override
	{ m_samples.FreeGpuBuffer(); }

	virtual bool HasGpuBuffer() override
	{ return m_samples.HasGpuBuffer(); }

	virtual void Resize(size_t size) override
	{ m_samples.resize(size); }

	virtual size_t size() const override
	{ return m_samples.size(); }

	virtual void clear() override
	{ m_samples.clear(); }

	virtual void PrepareForCpuAccess() override
	{ m_samples.PrepareForCpuAccess(); }

	virtual void PrepareForGpuAccess() override
	{ m_samples.PrepareForGpuAccess(); }

	virtual void MarkSamplesModifiedFromCpu() override
	{ m_samples.MarkModifiedFromCpu(); }

	virtual void MarkSamplesModifiedFromGpu() override
	{ m_samples.MarkModifiedFromGpu(); }

	virtual void MarkModifiedFromCpu() override
	{ MarkSamplesModifiedFromCpu(); }

	virtual void MarkModifiedFromGpu() override
	{ MarkSamplesModifiedFromGpu(); }

	/**
		@brief Passes a hint to the memory allocator about where our sample data is expected to be used

		@param hint	Hint value for expected usage
	 */
	void SetGpuAccessHint(enum AcceleratorBuffer<S>::UsageHint hint)
	{ m_samples.SetGpuAccessHint(hint); }
};

/**
	@brief A waveform sampled at irregular intervals.
	@ingroup datamodel
 */
template<class S>
class SparseWaveform : public SparseWaveformBase
{
public:

	/**
		@brief Creates a new sparse waveform

		@param name Internal name for this waveform, to be displayed in debug log messages etc
	 */
	SparseWaveform(const std::string& name = "")
	{
		Rename(name);

		//Default data to CPU/GPU mirror
		m_samples.SetCpuAccessHint(AcceleratorBuffer<S>::HINT_LIKELY);
		m_samples.SetGpuAccessHint(AcceleratorBuffer<S>::HINT_LIKELY);
		m_samples.PrepareForCpuAccess();
	}

	virtual void Rename(const std::string& name = "") override
	{
		if(name.empty())
		{
			m_samples.SetName(std::string("UniformWaveform<") + typeid(S).name() + ">.m_samples");
			m_offsets.SetName(std::string("SparseWaveform<") + typeid(S).name() + ">.m_offsets");
			m_durations.SetName(std::string("SparseWaveform<") + typeid(S).name() + ">.m_durations");
		}
		else
		{
			m_samples.SetName(name + ".m_samples");
			m_offsets.SetName(name + ".m_offsets");
			m_durations.SetName(name + ".m_durations");
		}
	}

	/**
		@brief Constructs a sparse waveform as a copy of a uniform waveform, marking all samples as 1 timebase unit in length
	 */
	SparseWaveform(UniformWaveform<S>& rhs)
	{
		m_samples.SetName(std::string("SparseWaveform<") + typeid(S).name() + ">.m_samples");
		m_offsets.SetName(std::string("SparseWaveform<") + typeid(S).name() + ">.m_offsets");
		m_durations.SetName(std::string("SparseWaveform<") + typeid(S).name() + ">.m_durations");

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

	virtual void FreeGpuMemory() override
	{
		m_offsets.FreeGpuBuffer();
		m_durations.FreeGpuBuffer();
		m_samples.FreeGpuBuffer();
	}

	virtual bool HasGpuBuffer() override
	{ return m_samples.HasGpuBuffer() || m_offsets.HasGpuBuffer() || m_durations.HasGpuBuffer(); }

	virtual void Resize(size_t size) override
	{
		m_offsets.resize(size);
		m_durations.resize(size);
		m_samples.resize(size);
	}

	virtual size_t size() const override
	{ return m_samples.size(); }

	virtual void clear() override
	{
		m_offsets.clear();
		m_durations.clear();
		m_samples.clear();
	}

	virtual void PrepareForCpuAccess() override
	{
		m_offsets.PrepareForCpuAccess();
		m_durations.PrepareForCpuAccess();
		m_samples.PrepareForCpuAccess();
	}

	virtual void PrepareForGpuAccess() override
	{
		m_offsets.PrepareForGpuAccess();
		m_durations.PrepareForGpuAccess();
		m_samples.PrepareForGpuAccess();
	}

	virtual void MarkSamplesModifiedFromCpu() override
	{ m_samples.MarkModifiedFromCpu(); }

	virtual void MarkSamplesModifiedFromGpu() override
	{ m_samples.MarkModifiedFromGpu(); }

	/**
		@brief Passes a hint to the memory allocator about where our sample data is expected to be used

		@param hint	Hint value for expected usage
	 */
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

/**
	@brief Returns true if the provided waveform is uniform, false if sparse
	@ingroup datamodel
 */
bool IsWaveformUniform(const SparseWaveformBase* /*unused*/)
{ return false; }

/**
	@brief Returns true if the provided waveform is uniform, false if sparse
	@ingroup datamodel
 */
bool IsWaveformUniform(const UniformWaveformBase* /*unused*/)
{ return true; }

//Helper methods for weighted averaging of samples
static float GetSampleTimesIndex(const UniformAnalogWaveform* wfm, ssize_t i);
static float GetSampleTimesIndex(const SparseAnalogWaveform* wfm, ssize_t i);

/**
	@brief Returns a single sample of the waveform multiplied by its own index
	@ingroup datamodel

	@param wfm	The source waveform
	@param i	Sample index
 */
float GetSampleTimesIndex(const UniformAnalogWaveform* wfm, ssize_t i)
{ return wfm->m_samples[i] * i; }

/**
	@brief Returns a single sample of the waveform multiplied by its own index
	@ingroup datamodel

	@param wfm	The source waveform
	@param i	Sample index
 */
float GetSampleTimesIndex(const SparseAnalogWaveform* wfm, ssize_t i)
{ return wfm->m_samples[i] * wfm->m_offsets[i]; }

/**
	@brief Returns the offset of a sample from the start of the waveform, in timebase ticks
	@ingroup datamodel

	This function does not convert timebase ticks to axis units or correct for trigger phase offset, and is meant
	for internal use within filters.

	@param wfm	The source waveform
	@param i	Sample index
 */
int64_t GetOffset(const SparseWaveformBase* wfm, size_t i)
{ return wfm->m_offsets[i]; }

/**
	@brief Returns the offset of a sample from the start of the waveform, in timebase ticks
	@ingroup datamodel

	This function does not convert timebase ticks to axis units or correct for trigger phase offset, and is meant
	for internal use within filters.

	@param wfm	The source waveform
	@param i	Sample index
 */
int64_t GetOffset(const UniformWaveformBase* /*wfm*/, size_t i)
{ return i; }

/**
	@brief Returns the duration of this sample, in timebase ticks
	@ingroup datamodel

	This function does not convert timebase ticks to axis units, and is meant for internal use within filters.

	@param wfm	The source waveform
	@param i	Sample index
 */
int64_t GetDuration(const SparseWaveformBase* wfm, size_t i)
{ return wfm->m_durations[i]; }

/**
	@brief Returns the duration of this sample, in timebase ticks
	@ingroup datamodel

	This function does not convert timebase ticks to axis units, and is meant for internal use within filters.

	@param wfm	The source waveform
	@param i	Sample index
 */
int64_t GetDuration(const UniformWaveformBase* /*wfm*/, size_t /*i*/)
{ return 1; }

/**
	@brief Returns the offset of a sample from the start of the waveform, in X axis units.
	@ingroup datamodel

	You should use this function to determine the final displayed timestamp of a sample.

	@param wfm	The source waveform
	@param i	Sample index
 */
template<class T>
int64_t GetOffsetScaled(T* wfm, size_t i)
{ return (GetOffset(wfm, i) * wfm->m_timescale) + wfm->m_triggerPhase; }

/**
	@brief Returns the duration of a sample, in X axis units.
	@ingroup datamodel

	You should use this function to determine the final displayed duration of a sample.

	@param wfm	The source waveform
	@param i	Sample index
 */
template<class T>
int64_t GetDurationScaled(T* wfm, size_t i)
{ return GetDuration(wfm, i) * wfm->m_timescale; }

/**
	@brief Helper for calling GetOffset() on a waveform which may be sparse or uniform
	@ingroup datamodel

	The caller is expected to dynamic_cast the waveform twice and pass both copies, one of which shoule be null.
 */
static int64_t GetOffset(const SparseWaveformBase* sparse, const UniformWaveformBase* uniform, size_t i);

/**
	@brief Helper for calling GetDuration() on a waveform which may be sparse or uniform
	@ingroup datamodel

	The caller is expected to dynamic_cast the waveform twice and pass both copies, one of which shoule be null.
 */
static int64_t GetDuration(const SparseWaveformBase* sparse, const UniformWaveformBase* uniform, size_t i);

/**
	@brief Helper for calling GetOffsetScaled() on a waveform which may be sparse or uniform
	@ingroup datamodel

	The caller is expected to dynamic_cast the waveform twice and pass both copies, one of which shoule be null.
 */
static int64_t GetOffsetScaled(const SparseWaveformBase* sparse, const UniformWaveformBase* uniform, size_t i);

/**
	@brief Helper for calling GetDurationScaled() on a waveform which may be sparse or uniform
	@ingroup datamodel

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
	@ingroup datamodel

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

/**
	@brief Look for a value greater than or equal to "value" in buf and return the index
	@ingroup datamodel
 */
template<class T>
size_t BinarySearchForGequal(T* buf, size_t len, T value);

/**
   @brief Find the index of the sample in a (possibly sparse) waveform that COULD include the time
   time_fs.
   @ingroup datamodel

   It is NOT GUARANTEED TO if the waveform is not continuous. Results are clamped to
   0 and wfm->size(), setting out_of_bounds if that happened. To be sure that the returned index
   refers to a sample that includes time_fs, check that `GetOffsetScaled(swaveform, index) +
   GetDurationScaled(swaveform, index) < time_fs`
 */
size_t GetIndexNearestAtOrBeforeTimestamp(WaveformBase* wfm, int64_t time_fs, bool& out_of_bounds);

/**
	@brief Gets the value of our channel at the specified timestamp (absolute, not waveform ticks)
	and interpolates if possible.
	@ingroup datamodel
 */
std::optional<float> GetValueAtTime(WaveformBase* waveform, int64_t time_fs, bool zero_hold_behavior);

/**
	@brief Gets the value of our channel at the specified timestamp (absolute, not waveform ticks).
	@ingroup datamodel
 */
std::optional<bool> GetDigitalValueAtTime(WaveformBase* waveform, int64_t time_fs);

/**
	@brief Gets the value of our channel at the specified timestamp (absolute, not waveform ticks).
	@ingroup datamodel
 */
std::optional<std::string> GetProtocolValueAtTime(WaveformBase* waveform, int64_t time_fs);

#pragma GCC diagnostic pop

#endif
