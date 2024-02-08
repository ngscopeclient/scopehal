/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2023 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Declaration of Filter
 */

#ifndef Filter_h
#define Filter_h

#include "OscilloscopeChannel.h"
#include "FlowGraphNode.h"

class QueueHandle;

/**
	@brief Describes a particular revision of a waveform

	Used to determine whether a filter input has changed, and thus cached state should be invalidated
 */
class WaveformCacheKey
{
public:
	WaveformCacheKey()
	: m_wfm(nullptr)
	, m_rev(0)
	{}

	WaveformCacheKey(WaveformBase* wfm)
	: m_wfm(wfm)
	, m_rev(wfm->m_revision)
	{}

	bool operator==(WaveformBase* wfm)
	{ return (m_wfm == wfm) && (m_rev == wfm->m_revision); }

	bool operator==(WaveformCacheKey wfm)
	{ return (m_wfm == wfm.m_wfm) && (m_rev == wfm.m_rev); }

	bool operator!=(WaveformBase* wfm)
	{ return (m_wfm != wfm) || (m_rev != wfm->m_revision); }

	bool operator!=(WaveformCacheKey wfm)
	{ return (m_wfm != wfm.m_wfm) || (m_rev != wfm.m_rev); }

	WaveformBase* m_wfm;
	uint64_t m_rev;
};

/**
	@brief Abstract base class for all filters and protocol decoders
 */
class Filter	: public OscilloscopeChannel
{
public:

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Construction and enumeration

	//Add new categories to the end of this list to maintain ABI compatibility with existing plugins
	enum Category
	{
		CAT_ANALYSIS,		//Signal integrity analysis
		CAT_BUS,			//Buses
		CAT_CLOCK,			//Clock stuff
		CAT_MATH,			//Basic math functions
		CAT_MEASUREMENT,	//Measurement functions
		CAT_MEMORY,			//Memory buses
		CAT_SERIAL,			//Serial communications
		CAT_MISC,			//anything not otherwise categorized
		CAT_POWER,			//Power analysis
		CAT_RF,				//Frequency domain analysis (FFT etc) and other RF stuff
		CAT_GENERATION,		//Waveform generation and synthesis
		CAT_EXPORT,			//Waveform export
		CAT_OPTICAL			//Optics
	};

	Filter(
		const std::string& color,
		Category cat,
		Unit xunit = Unit::UNIT_FS);
	virtual ~Filter();

	//Get all currently existing filters
	static std::set<Filter*> GetAllInstances()
	{ return m_filters; }

	//Get all currently existing filters
	static size_t GetNumInstances()
	{ return m_filters.size(); }

	/**
		@brief Removes this filter from the global list

		This is typically used for background filters used in GUI code to query stream names etc,
		but not actually used in the real filter graph.
	 */
	void HideFromList()
	{
		m_filters.erase(this);
		m_instanceCount[GetProtocolDisplayName()] --;
	}

	virtual void ClearStreams() override;
	virtual size_t AddStream(Unit yunit, const std::string& name, Stream::StreamType stype, uint8_t flags = 0) override;

	void AddProtocolStream(const std::string& name)
	{ AddStream(Unit(Unit::UNIT_COUNTS), name, Stream::STREAM_TYPE_PROTOCOL); }

	void AddDigitalStream(const std::string& name)
	{ AddStream(Unit(Unit::UNIT_COUNTS), name, Stream::STREAM_TYPE_DIGITAL); }

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Name generation

	virtual void SetDefaultName();

	/**
		@brief Specifies whether we're using an auto-generated name or not
	 */
	void UseDefaultName(bool use)
	{
		m_usingDefault = use;
		if(use)
			SetDefaultName();
	}

	bool IsUsingDefaultName()
	{ return m_usingDefault; }

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Reference counting

	virtual void AddRef() override;
	virtual void Release() override;

	size_t GetRefCount()
	{ return m_refcount; }

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Accessors

	Category GetCategory()
	{ return m_category; }

	virtual bool NeedsConfig();

	/**
		@brief Gets the display name of this protocol (for use in menus, save files, etc). Must be unique.
	 */
	virtual std::string GetProtocolDisplayName() =0;

public:
	/**
		@brief Clears any integrated data from past triggers (e.g. eye patterns).

		Most decoders shouldn't have to do anything for this.
	 */
	virtual void ClearSweeps();

	virtual void Refresh() override;

	//GPU accelerated refresh method
	virtual void Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue) override;

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Vertical scaling

	virtual void AutoscaleVertical(size_t stream);

	virtual float GetVoltageRange(size_t stream) override;
	virtual void SetVoltageRange(float range, size_t stream) override;

	virtual float GetOffset(size_t stream) override;
	virtual void SetOffset(float offset, size_t stream) override;

protected:
	std::vector<float> m_ranges;
	std::vector<float> m_offsets;

public:
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Serialization

	/**
		@brief Serialize this filter's configuration to a string
	 */
	virtual YAML::Node SerializeConfiguration(IDTable& table) override;

	virtual void LoadParameters(const YAML::Node& node, IDTable& table) override;
	virtual void LoadInputs(const YAML::Node& node, IDTable& table) override;

	virtual bool ShouldPersistWaveform() override;

protected:

	///Group used for the display menu
	Category m_category;

	///Indicates we're using an auto-generated name
	bool m_usingDefault;

	bool VerifyAllInputsOK(bool allowEmpty = false);
	bool VerifyInputOK(size_t i, bool allowEmpty = false);
	bool VerifyAllInputsOKAndUniformAnalog();
	bool VerifyAllInputsOKAndSparseAnalog();
	bool VerifyAllInputsOKAndSparseDigital();
	bool VerifyAllInputsOKAndSparseOrUniformDigital();

public:
	static int64_t GetNextEventTimestamp(SparseWaveformBase* wfm, size_t i, size_t len, int64_t timestamp);
	static int64_t GetNextEventTimestamp(UniformWaveformBase* wfm, size_t i, size_t len, int64_t timestamp);

	static int64_t GetNextEventTimestamp(
		SparseWaveformBase* swfm, UniformWaveformBase* uwfm, size_t i, size_t len, int64_t timestamp)
	{
		if(swfm)
			return GetNextEventTimestamp(swfm, i, len, timestamp);
		else
			return GetNextEventTimestamp(uwfm, i, len, timestamp);
	}

	static void AdvanceToTimestamp(SparseWaveformBase* wfm, size_t& i, size_t len, int64_t timestamp);
	static void AdvanceToTimestamp(UniformWaveformBase* wfm, size_t& i, size_t len, int64_t timestamp);

	static void AdvanceToTimestamp(
		SparseWaveformBase* swfm, UniformWaveformBase* uwfm, size_t& i, size_t len, int64_t timestamp)
	{
		if(swfm)
			AdvanceToTimestamp(swfm, i, len, timestamp);
		else
			AdvanceToTimestamp(uwfm, i, len, timestamp);
	}

	static int64_t GetNextEventTimestampScaled(SparseWaveformBase* wfm, size_t i, size_t len, int64_t timestamp);
	static int64_t GetNextEventTimestampScaled(UniformWaveformBase* wfm, size_t i, size_t len, int64_t timestamp);
	static void AdvanceToTimestampScaled(SparseWaveformBase* wfm, size_t& i, size_t len, int64_t timestamp);
	static void AdvanceToTimestampScaled(UniformWaveformBase* wfm, size_t& i, size_t len, int64_t timestamp);

	static void AdvanceToTimestampScaled(
		SparseWaveformBase* swfm, UniformWaveformBase* uwfm, size_t& i, size_t len, int64_t timestamp)
	{
		if(swfm)
			AdvanceToTimestampScaled(swfm, i, len, timestamp);
		else
			AdvanceToTimestampScaled(uwfm, i, len, timestamp);
	}

	static int64_t GetNextEventTimestampScaled(
		SparseWaveformBase* swfm, UniformWaveformBase* uwfm, size_t i, size_t len, int64_t timestamp)
	{
		if(swfm)
			return GetNextEventTimestampScaled(swfm, i, len, timestamp);
		else
			return GetNextEventTimestampScaled(uwfm, i, len, timestamp);
	}

protected:
	UniformAnalogWaveform* SetupEmptyUniformAnalogOutputWaveform(WaveformBase* din, size_t stream, bool clear=true);
	SparseAnalogWaveform* SetupEmptySparseAnalogOutputWaveform(WaveformBase* din, size_t stream, bool clear=true);
	UniformDigitalWaveform* SetupEmptyUniformDigitalOutputWaveform(WaveformBase* din, size_t stream);
	SparseDigitalWaveform* SetupEmptySparseDigitalOutputWaveform(WaveformBase* din, size_t stream);
	SparseAnalogWaveform* SetupSparseOutputWaveform(SparseWaveformBase* din, size_t stream, size_t skipstart, size_t skipend);
	SparseDigitalWaveform* SetupSparseDigitalOutputWaveform(SparseWaveformBase* din, size_t stream, size_t skipstart, size_t skipend);

public:
	//Helpers for sub-sample interpolation

	/**
		@brief Interpolates the actual time of a threshold crossing between two samples

		Simple linear interpolation for now (TODO sinc)

		@return Interpolated crossing time. 0=a, 1=a+1, fractional values are in between.

		TODO: validate that this works correctly for sparsely sampled waveforms?
	 */
	template<class T>
	__attribute__((noinline))
	static float InterpolateTime(T* cap, size_t a, float voltage)
	{
		AssertTypeIsAnalogWaveform(cap);

		//If the voltage isn't between the two points, abort
		float fa = cap->m_samples[a];
		float fb = cap->m_samples[a+1];
		bool ag = (fa > voltage);
		bool bg = (fb > voltage);
		if( (ag && bg) || (!ag && !bg) )
			return 0;

		//no need to divide by time, sample spacing is normalized to 1 timebase unit
		float slope = (fb - fa);
		float delta = voltage - fa;
		return delta / slope;
	}

	static float InterpolateTime(SparseAnalogWaveform* s, UniformAnalogWaveform* u, size_t a, float voltage)
	{
		if(s)
			return InterpolateTime(s, a, voltage);
		else
			return InterpolateTime(u, a, voltage);
	}

	static float InterpolateTime(UniformAnalogWaveform* p, UniformAnalogWaveform* n, size_t a, float voltage);
	static float InterpolateTime(SparseAnalogWaveform* p, SparseAnalogWaveform* n, size_t a, float voltage);

	static float InterpolateTime(
		SparseAnalogWaveform* sp,
		UniformAnalogWaveform* up,
		SparseAnalogWaveform* sn,
		UniformAnalogWaveform* un,
		size_t a, float voltage)
	{
		if(sp)
			return InterpolateTime(sp, sn, a, voltage);
		else
			return InterpolateTime(up, un, a, voltage);
	}

	static float InterpolateValue(SparseAnalogWaveform* cap, size_t index, float frac_ticks);
	static float InterpolateValue(UniformAnalogWaveform* cap, size_t index, float frac_ticks);

	//Helpers for more complex measurements
	//TODO: create some process for caching this so we don't waste CPU time

	/**
		@brief Gets the lowest voltage of a waveform
	 */
	template<class T>
	__attribute__((noinline))
	static float GetMinVoltage(T* cap)
	{
		AssertTypeIsAnalogWaveform(cap);

		//Loop over samples and find the minimum
		float tmp = FLT_MAX;
		for(float f : cap->m_samples)
		{
			if(f < tmp)
				tmp = f;
		}
		return tmp;
	}

	/**
		@brief Gets the lowest voltage of a waveform
	 */
	static float GetMinVoltage(SparseAnalogWaveform* s, UniformAnalogWaveform* u)
	{
		if(s)
			return GetMinVoltage(s);
		else
			return GetMinVoltage(u);
	}

	/**
		@brief Gets the highest voltage of a waveform
	 */
	template<class T>
	__attribute__((noinline))
	static float GetMaxVoltage(T* cap)
	{
		AssertTypeIsAnalogWaveform(cap);

		//Loop over samples and find the maximum
		float tmp = -FLT_MAX;
		for(float f : cap->m_samples)
		{
			if(f > tmp)
				tmp = f;
		}
		return tmp;
	}

	/**
		@brief Gets the lowest voltage of a waveform
	 */
	static float GetMaxVoltage(SparseAnalogWaveform* s, UniformAnalogWaveform* u)
	{
		if(s)
			return GetMaxVoltage(s);
		else
			return GetMaxVoltage(u);
	}

	/**
		@brief Gets the most probable "0" level for a digital waveform
	 */
	template<class T>
	__attribute__((noinline))
	static float GetBaseVoltage(T* cap)
	{
		AssertTypeIsAnalogWaveform(cap);

		float vmin = GetMinVoltage(cap);
		float vmax = GetMaxVoltage(cap);
		float delta = vmax - vmin;
		const int nbins = 100;
		auto hist = MakeHistogram(cap, vmin, vmax, nbins);

		//Find the highest peak in the first quarter of the histogram
		size_t binval = 0;
		int idx = 0;
		for(int i=0; i<(nbins/4); i++)
		{
			if(hist[i] > binval)
			{
				binval = hist[i];
				idx = i;
			}
		}

		float fbin = (idx + 0.5f)/nbins;
		return fbin*delta + vmin;
	}

	/**
		@brief Gets the base voltage of a waveform which may be sparse or uniform
	 */
	static float GetBaseVoltage(SparseAnalogWaveform* swfm, UniformAnalogWaveform* uwfm)
	{
		if(swfm)
			return GetBaseVoltage(swfm);
		else
			return GetBaseVoltage(uwfm);
	}

	/**
		@brief Gets the most probable "1" level for a digital waveform
	 */
	template<class T>
	__attribute__((noinline))
	static float GetTopVoltage(T* cap)
	{
		AssertTypeIsAnalogWaveform(cap);

		float vmin = GetMinVoltage(cap);
		float vmax = GetMaxVoltage(cap);
		float delta = vmax - vmin;
		const int nbins = 100;
		auto hist = MakeHistogram(cap, vmin, vmax, nbins);

		//Find the highest peak in the third quarter of the histogram
		size_t binval = 0;
		int idx = 0;
		for(int i=(nbins*3)/4; i<nbins; i++)
		{
			if(hist[i] > binval)
			{
				binval = hist[i];
				idx = i;
			}
		}

		float fbin = (idx + 0.5f)/nbins;
		return fbin*delta + vmin;
	}

	/**
		@brief Gets the top voltage of a waveform which may be sparse or uniform
	 */
	static float GetTopVoltage(SparseAnalogWaveform* swfm, UniformAnalogWaveform* uwfm)
	{
		if(swfm)
			return GetTopVoltage(swfm);
		else
			return GetTopVoltage(uwfm);
	}

	/**
		@brief Gets the average voltage of a waveform
	 */
	template<class T>
	__attribute__((noinline))
	static float GetAvgVoltage(T* cap)
	{
		AssertTypeIsAnalogWaveform(cap);

		//Loop over samples and find the average
		//TODO: more numerically stable summation algorithm for deep captures
		double sum = 0;
		for(float f : cap->m_samples)
			sum += f;
		return sum / cap->m_samples.size();
	}

	/**
		@brief Gets the average voltage of a waveform which may be sparse or uniform
	 */
	static float GetAvgVoltage(SparseAnalogWaveform* swfm, UniformAnalogWaveform* uwfm)
	{
		if(swfm)
			return GetAvgVoltage(swfm);
		else
			return GetAvgVoltage(uwfm);
	}

	/**
		@brief Makes a histogram from a waveform with the specified number of bins.

		Any values outside the range are clamped (put in bin 0 or bins-1 as appropriate).

		@param low	Low endpoint of the histogram (volts)
		@param high High endpoint of the histogram (volts)
		@param bins	Number of histogram bins
	 */
	template<class T>
	__attribute__((noinline))
	static std::vector<size_t> MakeHistogram(T* cap, float low, float high, size_t bins)
	{
		AssertTypeIsAnalogWaveform(cap);

		std::vector<size_t> ret;
		for(size_t i=0; i<bins; i++)
			ret.push_back(0);

		//Early out if we have zero span
		if(bins == 0)
			return ret;

		float delta = high-low;

		for(float v : cap->m_samples)
		{
			float fbin = (v-low) / delta;
			size_t bin = floor(fbin * bins);
			if(fbin < 0)
				bin = 0;
			else
				bin = std::min(bin, bins-1);
			ret[bin] ++;
		}

		return ret;
	}

	/**
		@brief Makes a histogram from a waveform with the specified number of bins.

		Any values outside the range are clamped (put in bin 0 or bins-1 as appropriate).

		@param low	Low endpoint of the histogram (volts)
		@param high High endpoint of the histogram (volts)
		@param bins	Number of histogram bins
	 */
	static std::vector<size_t> MakeHistogram(
		SparseAnalogWaveform* s, UniformAnalogWaveform* u, float low, float high, size_t bins)
	{
		if(s)
			return MakeHistogram(s, low, high, bins);
		else
			return MakeHistogram(u, low, high, bins);
	}

	/**
		@brief Makes a histogram from a waveform with the specified number of bins.

		Any values outside the range are discarded.

		@param low	Low endpoint of the histogram (volts)
		@param high High endpoint of the histogram (volts)
		@param bins	Number of histogram bins
	 */
	template<class T>
	__attribute__((noinline))
	static std::vector<size_t> MakeHistogramClipped(T* cap, float low, float high, size_t bins)
	{
		AssertTypeIsAnalogWaveform(cap);

		std::vector<size_t> ret;
		for(size_t i=0; i<bins; i++)
			ret.push_back(0);

		//Early out if we have zero span
		if(bins == 0)
			return ret;

		float delta = high-low;

		for(float v : cap->m_samples)
		{
			float fbin = (v-low) / delta;
			// must cast through a signed int type to avoid UB (e.g. saturates to 0 on arm64) [conv.fpint]
			size_t bin = static_cast<ssize_t>(floor(fbin * bins));
			if(bin >= bins)	//negative values wrap to huge positive and get caught here
				continue;
			ret[bin] ++;
		}

		return ret;
	}

	/**
		@brief Samples a waveform on all edges of a clock

		The sampling rate of the data and clock signals need not be equal or uniform.

		The sampled waveform is sparse and has a time scale in femtoseconds,
		regardless of the incoming waveform's time scale and sampling uniformity.

		@param data		The data signal to sample. Can be be sparse or uniform of any type.
		@param clock	The clock signal to use. Must be sparse or uniform digital.
		@param samples	Output waveform. Must be sparse and same data type as data.
	 */
	template<class T, class R, class S>
	__attribute__((noinline))
	static void SampleOnAnyEdges(T* data, R* clock, SparseWaveform<S>& samples)
	{
		//Compile-time check to make sure inputs are correct types
		AssertTypeIsDigitalWaveform(clock);
		AssertSampleTypesAreSame(data, &samples);

		samples.clear();
		samples.SetGpuAccessHint(AcceleratorBuffer<S>::HINT_NEVER);	//assume we're being used as part of a CPU-side filter
		samples.PrepareForCpuAccess();

		//TODO: split up into blocks and multithread?
		//TODO: AVX vcompress?

		size_t len = clock->size();
		size_t dlen = data->size();

		size_t ndata = 0;
		for(size_t i=1; i<len; i++)
		{
			//Throw away clock samples until we find an edge
			if(clock->m_samples[i] == clock->m_samples[i-1])
				continue;

			//Throw away data samples until the data is synced with us
			int64_t clkstart = GetOffsetScaled(clock, i);
			while( (ndata+1 < dlen) && (GetOffsetScaled(data, ndata+1) < clkstart) )
				ndata ++;
			if(ndata >= dlen)
				break;

			//Add the new sample
			samples.m_offsets.push_back(clkstart);
			samples.m_samples.push_back(data->m_samples[ndata]);
		}

		//Compute sample durations
		#ifdef __x86_64__
		if(g_hasAvx2)
			FillDurationsAVX2(samples);
		else
		#endif
			FillDurationsGeneric(samples);

		samples.MarkModifiedFromCpu();
	}

	/**
		@brief Samples a waveform on all edges of a clock

		The sampling rate of the data and clock signals need not be equal or uniform.

		The sampled waveform is sparse and has a time scale in femtoseconds,
		regardless of the incoming waveform's time scale and sampling uniformity.

		@param data		The data signal to sample. Can be be sparse or uniform of any type.
		@param clock	The clock signal to use. Must be sparse or uniform digital.
		@param samples	Output waveform. Must be sparse and same data type as data.
	 */
	template<class T>
	__attribute__((noinline))
	static void SampleOnAnyEdgesBase(WaveformBase* data, WaveformBase* clock, SparseWaveform<T>& samples)
	{
		data->PrepareForCpuAccess();
		clock->PrepareForCpuAccess();
		samples.PrepareForCpuAccess();

		auto udata = dynamic_cast<UniformWaveform<T>*>(data);
		auto sdata = dynamic_cast<SparseWaveform<T>*>(data);

		auto uclock = dynamic_cast<UniformDigitalWaveform*>(clock);
		auto sclock = dynamic_cast<SparseDigitalWaveform*>(clock);

		if(udata && uclock)
			SampleOnAnyEdges(udata, uclock, samples);
		else if(udata && sclock)
			SampleOnAnyEdges(udata, sclock, samples);
		else if(sdata && sclock)
			SampleOnAnyEdges(sdata, sclock, samples);
		else if(sdata && uclock)
			SampleOnAnyEdges(sdata, uclock, samples);
	}

	/**
		@brief Samples a waveform on the rising edges of a clock

		The sampling rate of the data and clock signals need not be equal or uniform.

		The sampled waveform is sparse and has a time scale in femtoseconds,
		regardless of the incoming waveform's time scale and sampling uniformity.

		@param data		The data signal to sample. Can be be sparse or uniform of any type.
		@param clock	The clock signal to use. Must be sparse or uniform digital.
		@param samples	Output waveform. Must be sparse and same data type as data.
	 */
	template<class T, class R, class S>
	__attribute__((noinline))
	static void SampleOnRisingEdges(T* data, R* clock, SparseWaveform<S>& samples)
	{
		//Compile-time check to make sure inputs are correct types
		AssertTypeIsDigitalWaveform(clock);
		AssertTypeIsSparseWaveform(&samples);
		AssertSampleTypesAreSame(data, &samples);

		samples.clear();
		samples.SetGpuAccessHint(AcceleratorBuffer<S>::HINT_NEVER);	//assume we're being used as part of a CPU-side filter

		//TODO: split up into blocks and multithread?
		//TODO: AVX vcompress?

		size_t len = clock->size();
		size_t dlen = data->size();

		size_t ndata = 0;
		for(size_t i=1; i<len; i++)
		{
			//Throw away clock samples until we find a rising edge
			if(!(clock->m_samples[i] && !clock->m_samples[i-1]))
				continue;

			//Throw away data samples until the data is synced with us
			int64_t clkstart = GetOffsetScaled(clock, i);
			while( (ndata+1 < dlen) && (GetOffsetScaled(data, ndata+1) < clkstart) )
				ndata ++;
			if(ndata >= dlen)
				break;

			//Add the new sample
			samples.m_offsets.push_back(clkstart);
			samples.m_samples.push_back(data->m_samples[ndata]);
		}

		//Compute sample durations
		#ifdef __x86_64__
		if(g_hasAvx2)
			FillDurationsAVX2(samples);
		else
		#endif
			FillDurationsGeneric(samples);

		samples.MarkModifiedFromCpu();
	}

	/**
		@brief Samples a waveform on rising edges of a clock

		The sampling rate of the data and clock signals need not be equal or uniform.

		The sampled waveform is sparse and has a time scale in femtoseconds,
		regardless of the incoming waveform's time scale and sampling uniformity.

		@param data		The data signal to sample. Can be be sparse or uniform of any type.
		@param clock	The clock signal to use. Must be sparse or uniform digital.
		@param samples	Output waveform. Must be sparse and same data type as data.
	 */
	template<class T>
	__attribute__((noinline))
	static void SampleOnRisingEdgesBase(WaveformBase* data, WaveformBase* clock, SparseWaveform<T>& samples)
	{
		data->PrepareForCpuAccess();
		clock->PrepareForCpuAccess();
		samples.PrepareForCpuAccess();

		auto udata = dynamic_cast<UniformWaveform<T>*>(data);
		auto sdata = dynamic_cast<SparseWaveform<T>*>(data);

		auto uclock = dynamic_cast<UniformDigitalWaveform*>(clock);
		auto sclock = dynamic_cast<SparseDigitalWaveform*>(clock);

		if(udata && uclock)
			SampleOnRisingEdges(udata, uclock, samples);
		else if(udata && sclock)
			SampleOnRisingEdges(udata, sclock, samples);
		else if(sdata && sclock)
			SampleOnRisingEdges(sdata, sclock, samples);
		else if(sdata && uclock)
			SampleOnRisingEdges(sdata, uclock, samples);
	}

	/**
		@brief Samples a waveform on the falling edges of a clock

		The sampling rate of the data and clock signals need not be equal or uniform.

		The sampled waveform is sparse and has a time scale in femtoseconds,
		regardless of the incoming waveform's time scale and sampling uniformity.

		@param data		The data signal to sample. Can be be sparse or uniform of any type.
		@param clock	The clock signal to use. Must be sparse or uniform digital.
		@param samples	Output waveform. Must be sparse and same data type as data.
	 */
	template<class T, class R, class S>
	__attribute__((noinline))
	static void SampleOnFallingEdges(T* data, R* clock, SparseWaveform<S>& samples)
	{
		//Compile-time check to make sure inputs are correct types
		AssertTypeIsDigitalWaveform(clock);
		AssertTypeIsSparseWaveform(&samples);
		AssertSampleTypesAreSame(data, &samples);

		samples.clear();
		samples.SetGpuAccessHint(AcceleratorBuffer<S>::HINT_NEVER);	//assume we're being used as part of a CPU-side filter

		//TODO: split up into blocks and multithread?
		//TODO: AVX vcompress?

		size_t len = clock->size();
		size_t dlen = data->size();

		size_t ndata = 0;
		for(size_t i=1; i<len; i++)
		{
			//Throw away clock samples until we find a falling edge
			if(!(!clock->m_samples[i] && clock->m_samples[i-1]))
				continue;

			//Throw away data samples until the data is synced with us
			int64_t clkstart = GetOffsetScaled(clock, i);
			while( (ndata+1 < dlen) && (GetOffsetScaled(data, ndata+1) < clkstart) )
				ndata ++;
			if(ndata >= dlen)
				break;

			//Add the new sample
			samples.m_offsets.push_back(clkstart);
			samples.m_samples.push_back(data->m_samples[ndata]);
		}

		//Compute sample durations
		#ifdef __x86_64__
		if(g_hasAvx2)
			FillDurationsAVX2(samples);
		else
		#endif
			FillDurationsGeneric(samples);

		samples.MarkModifiedFromCpu();
	}

	/**
		@brief Samples an analog waveform on all edges of a clock, interpolating linearly to get sub-sample accuracy.

		The sampling rate of the data and clock signals need not be equal or uniform.

		The sampled waveform has a time scale in femtoseconds regardless of the incoming waveform's time scale.

		@param data		The data signal to sample
		@param clock	The clock signal to use
		@param samples	Output waveform
	 */
	template<class T, class R>
	__attribute__((noinline))
	static void SampleOnAnyEdgesWithInterpolation(T* data, R* clock, SparseAnalogWaveform& samples)
	{
		//Compile-time check to make sure inputs are correct types
		AssertTypeIsAnalogWaveform(data);
		AssertTypeIsDigitalWaveform(clock);

		samples.clear();
		samples.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_NEVER);	//assume we're being used as part of a CPU-side filter

		//TODO: split up into blocks and multithread?
		//TODO: AVX vcompress

		size_t len = clock->size();
		size_t dlen = data->size();

		size_t ndata = 0;
		for(size_t i=1; i<len; i++)
		{
			//Throw away clock samples until we find an edge
			if(clock->m_samples[i] == clock->m_samples[i-1])
				continue;

			//Throw away data samples until the data is synced with us
			int64_t clkstart = GetOffsetScaled(clock, i);
			while( (ndata+1 < dlen) && (GetOffsetScaled(data, ndata+1) < clkstart) )
				ndata ++;
			if(ndata >= dlen)
				break;

			//Find the fractional position of the clock edge
			int64_t tsample = GetOffsetScaled(data, ndata);
			int64_t delta = clkstart - tsample;
			float frac = delta * 1.0 / data->m_timescale;

			//Add the new sample
			samples.m_offsets.push_back(clkstart);
			samples.m_samples.push_back(InterpolateValue(data, ndata, frac));
		}

		//Compute sample durations
		#ifdef __x86_64__
		if(g_hasAvx2)
			FillDurationsAVX2(samples);
		else
		#endif
			FillDurationsGeneric(samples);

		samples.MarkModifiedFromCpu();
	}

	/**
		@brief Samples an analog waveform on all edges of a clock, interpolating linearly to get sub-sample accuracy.

		The sampling rate of the data and clock signals need not be equal or uniform.

		The sampled waveform is sparse and has a time scale in femtoseconds,
		regardless of the incoming waveform's time scale and sampling uniformity.

		@param data		The data signal to sample. Can be be sparse or uniform of any type.
		@param clock	The clock signal to use. Must be sparse or uniform digital.
		@param samples	Output waveform. Must be sparse and same data type as data.
	 */
	template<class T>
	__attribute__((noinline))
	static void SampleOnAnyEdgesBaseWithInterpolation(WaveformBase* data, WaveformBase* clock, SparseWaveform<T>& samples)
	{
		data->PrepareForCpuAccess();
		clock->PrepareForCpuAccess();
		samples.PrepareForCpuAccess();

		auto udata = dynamic_cast<UniformWaveform<T>*>(data);
		auto sdata = dynamic_cast<SparseWaveform<T>*>(data);

		auto uclock = dynamic_cast<UniformDigitalWaveform*>(clock);
		auto sclock = dynamic_cast<SparseDigitalWaveform*>(clock);

		if(udata && uclock)
			SampleOnAnyEdgesWithInterpolation(udata, uclock, samples);
		else if(udata && sclock)
			SampleOnAnyEdgesWithInterpolation(udata, sclock, samples);
		else if(sdata && sclock)
			SampleOnAnyEdgesWithInterpolation(sdata, sclock, samples);
		else if(sdata && uclock)
			SampleOnAnyEdgesWithInterpolation(sdata, uclock, samples);
	}

	/**
		@brief Prepares a sparse or uniform analog waveform for CPU access
	 */
	template<class T>
	static void PrepareForCpuAccess(SparseWaveform<T>* s, UniformWaveform<T>* u)
	{
		if(s)
			s->PrepareForCpuAccess();
		else
			u->PrepareForCpuAccess();
	}

	/**
		@brief Prepares a sparse or uniform analog waveform for GPU access
	 */
	template<class T>
	static void PrepareForGpuAccess(SparseWaveform<T>* s, UniformWaveform<T>* u)
	{
		if(s)
			s->PrepareForGpuAccess();
		else
			u->PrepareForGpuAccess();
	}

	static void FindRisingEdges(UniformAnalogWaveform* data, float threshold, std::vector<int64_t>& edges);
	static void FindRisingEdges(SparseAnalogWaveform* data, float threshold, std::vector<int64_t>& edges);
	static void FindZeroCrossings(SparseAnalogWaveform* data, float threshold, std::vector<int64_t>& edges);
	static void FindZeroCrossings(UniformAnalogWaveform* data, float threshold, std::vector<int64_t>& edges);
	static void FindZeroCrossings(UniformDigitalWaveform* data, std::vector<int64_t>& edges);
	static void FindZeroCrossings(SparseDigitalWaveform* data, std::vector<int64_t>& edges);
	static void FindRisingEdges(UniformDigitalWaveform* data, std::vector<int64_t>& edges);
	static void FindRisingEdges(SparseDigitalWaveform* data, std::vector<int64_t>& edges);
	static void FindFallingEdges(UniformDigitalWaveform* data, std::vector<int64_t>& edges);
	static void FindFallingEdges(SparseDigitalWaveform* data, std::vector<int64_t>& edges);
	static void FindPeaks(UniformAnalogWaveform* data, float peak_threshold, std::vector<int64_t>& peak_indices);
	static void FindPeaks(SparseAnalogWaveform* data, float peak_threshold, std::vector<int64_t>& peak_indices);

	static void FindZeroCrossingsBase(WaveformBase* data, float threshold, std::vector<int64_t>& edges)
	{
		auto udata = dynamic_cast<UniformAnalogWaveform*>(data);
		auto sdata = dynamic_cast<SparseAnalogWaveform*>(data);

		if(udata)
			FindZeroCrossings(udata, threshold, edges);
		else
			FindZeroCrossings(sdata, threshold, edges);
	}

	static void FindRisingEdges(
		SparseDigitalWaveform* sdata, UniformDigitalWaveform* udata, std::vector<int64_t>& edges)
	{
		if(sdata)
			FindRisingEdges(sdata, edges);
		else
			FindRisingEdges(udata, edges);
	}

	static void FindFallingEdges(
		SparseDigitalWaveform* sdata, UniformDigitalWaveform* udata, std::vector<int64_t>& edges)
	{
		if(sdata)
			FindFallingEdges(sdata, edges);
		else
			FindFallingEdges(udata, edges);
	}

	static void FindPeaks(
		SparseAnalogWaveform* sdata, UniformAnalogWaveform* udata, float peak_threshold, std::vector<int64_t>& peak_indices)
	{
		if(sdata)
			FindPeaks(sdata, peak_threshold, peak_indices);
		else
			FindPeaks(udata, peak_threshold, peak_indices);
	}

	static void FindZeroCrossings(
		SparseAnalogWaveform* sdata, UniformAnalogWaveform* udata, float threshold, std::vector<int64_t>& edges)
	{
		if(sdata)
			FindZeroCrossings(sdata, threshold, edges);
		else
			FindZeroCrossings(udata, threshold, edges);
	}

	static void FindZeroCrossings(
		SparseDigitalWaveform* sdata, UniformDigitalWaveform* udata, std::vector<int64_t>& edges)
	{
		if(sdata)
			FindZeroCrossings(sdata, edges);
		else
			FindZeroCrossings(udata, edges);
	}

	static void ClearAnalysisCache();

protected:
	//Helpers for sparse waveforms
	static void FillDurationsGeneric(SparseWaveformBase& wfm);
#ifdef __x86_64__
	static void FillDurationsAVX2(SparseWaveformBase& wfm);
#endif

public:
	sigc::signal<void()> signal_outputsChanged()
	{ return m_outputsChangedSignal; }

protected:
	///@brief Signal emitted when the set of output streams changes
	sigc::signal<void()> m_outputsChangedSignal;

	/**
		@brief Instance number (for auto naming)

		Starts at 0 for the first filter of a given class type created, then increments
	 */
	unsigned int m_instanceNum;

public:
	typedef Filter* (*CreateProcType)(const std::string&);
	static void DoAddDecoderClass(const std::string& name, CreateProcType proc);

	static void EnumProtocols(std::vector<std::string>& names);
	static Filter* CreateFilter(const std::string& protocol, const std::string& color = "#ffffff");

protected:
	//Class enumeration
	typedef std::map< std::string, CreateProcType > CreateMapType;
	static CreateMapType m_createprocs;

	//Object enumeration
	static std::set<Filter*> m_filters;

	//Instance naming
	static std::map<std::string, unsigned int> m_instanceCount;

	//Caching
	static std::mutex m_cacheMutex;
	static std::map<std::pair<WaveformBase*, float>, std::vector<int64_t> > m_zeroCrossingCache;
};

#define PROTOCOL_DECODER_INITPROC(T) \
	static Filter* CreateInstance(const std::string& color) \
	{ \
		return new T(color); \
	} \
	virtual std::string GetProtocolDisplayName() override \
	{ return GetProtocolName(); }

#define AddDecoderClass(T) Filter::DoAddDecoderClass(T::GetProtocolName(), T::CreateInstance)

#endif
