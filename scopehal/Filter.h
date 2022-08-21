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
	@brief Declaration of Filter
 */

#ifndef Filter_h
#define Filter_h

#include "OscilloscopeChannel.h"
#include "FlowGraphNode.h"

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
		CAT_GENERATION		//Waveform generation and synthesis
	};

	Filter(
		const std::string& color,
		Category cat,
		Unit xunit = Unit::UNIT_FS,
		const std::string& kernelPath = "",
		const std::string& kernelName = "");
	virtual ~Filter();

	//Get all currently existing filters
	static std::set<Filter*> GetAllInstances()
	{ return m_filters; }

	virtual void ClearStreams();
	virtual void AddStream(Unit yunit, const std::string& name, Stream::StreamType stype);

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

	virtual void AddRef();
	virtual void Release();

	size_t GetRefCount()
	{ return m_refcount; }

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Accessors

	virtual bool IsScalarOutput();

	Category GetCategory()
	{ return m_category; }

	virtual bool NeedsConfig();

	/**
		@brief Gets the display name of this protocol (for use in menus, save files, etc). Must be unique.
	 */
	virtual std::string GetProtocolDisplayName() =0;

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Evaluation

	//Legacy CPU implementation
	virtual void Refresh();

	//GPU accelerated refresh method
	virtual void Refresh(vk::raii::CommandBuffer& cmdBuf, vk::raii::Queue& queue);

	/**
		@brief Clears any integrated data from past triggers (e.g. eye patterns).

		Most decoders shouldn't have to do anything for this.
	 */
	virtual void ClearSweeps();

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Vertical scaling

	virtual void AutoscaleVertical(size_t stream);

	virtual float GetVoltageRange(size_t stream);
	virtual void SetVoltageRange(float range, size_t stream);

	virtual float GetOffset(size_t stream);
	virtual void SetOffset(float offset, size_t stream);

protected:
	std::vector<float> m_ranges;
	std::vector<float> m_offsets;

public:

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Accelerated waveform accessors

	enum DataLocation
	{
		LOC_CPU,
		LOC_GPU,
		LOC_DONTCARE
	};

	virtual DataLocation GetInputLocation();

public:
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Serialization

	/**
		@brief Serialize this decoder's configuration to a string
	 */
	virtual std::string SerializeConfiguration(IDTable& table, size_t indent = 8);

	virtual void LoadParameters(const YAML::Node& node, IDTable& table);

protected:

	///Group used for the display menu
	Category m_category;

	///Indicates we're using an auto-generated name
	bool m_usingDefault;

	bool VerifyAllInputsOK(bool allowEmpty = false);
	bool VerifyInputOK(size_t i, bool allowEmpty = false);
	bool VerifyAllInputsOKAndAnalog();
	bool VerifyAllInputsOKAndDigital();

public:
	static int64_t GetNextEventTimestamp(WaveformBase* wfm, size_t i, size_t len, int64_t timestamp);
	static void AdvanceToTimestamp(WaveformBase* wfm, size_t& i, size_t len, int64_t timestamp);
	static int64_t GetNextEventTimestampScaled(WaveformBase* wfm, size_t i, size_t len, int64_t timestamp);
	static void AdvanceToTimestampScaled(WaveformBase* wfm, size_t& i, size_t len, int64_t timestamp);

protected:
	AnalogWaveform* SetupEmptyOutputWaveform(WaveformBase* din, size_t stream, bool clear=true);
	DigitalWaveform* SetupEmptyDigitalOutputWaveform(WaveformBase* din, size_t stream);
	AnalogWaveform* SetupOutputWaveform(WaveformBase* din, size_t stream, size_t skipstart, size_t skipend);
	DigitalWaveform* SetupDigitalOutputWaveform(WaveformBase* din, size_t stream, size_t skipstart, size_t skipend);

public:
	//Helpers for sub-sample interoplation
	static float InterpolateTime(AnalogWaveform* cap, size_t a, float voltage);
	static float InterpolateTime(AnalogWaveform* p, AnalogWaveform* n, size_t a, float voltage);
	static float InterpolateValue(AnalogWaveform* cap, size_t index, float frac_ticks);

	//Helpers for more complex measurements
	//TODO: create some process for caching this so we don't waste CPU time
	static float GetMinVoltage(AnalogWaveform* cap);
	static float GetMaxVoltage(AnalogWaveform* cap);
	static float GetBaseVoltage(AnalogWaveform* cap);
	static float GetTopVoltage(AnalogWaveform* cap);
	static float GetAvgVoltage(AnalogWaveform* cap);
	static std::vector<size_t> MakeHistogram(AnalogWaveform* cap, float low, float high, size_t bins);
	static std::vector<size_t> MakeHistogramClipped(AnalogWaveform* cap, float low, float high, size_t bins);

	//Samples a channel on the edges of another channel.
	//The two channels need not be the same sample rate.
	static void SampleOnAnyEdges(AnalogWaveform* data, DigitalWaveform* clock, AnalogWaveform& samples);
	static void SampleOnAnyEdgesWithInterpolation(AnalogWaveform* data, DigitalWaveform* clock, AnalogWaveform& samples);
	static void SampleOnAnyEdges(DigitalWaveform* data, DigitalWaveform* clock, DigitalWaveform& samples);
	static void SampleOnAnyEdges(DigitalBusWaveform* data, DigitalWaveform* clock, DigitalBusWaveform& samples);
	static void SampleOnRisingEdges(DigitalWaveform* data, DigitalWaveform* clock, DigitalWaveform& samples);
	static void SampleOnRisingEdges(DigitalBusWaveform* data, DigitalWaveform* clock, DigitalBusWaveform& samples);
	static void SampleOnFallingEdges(DigitalWaveform* data, DigitalWaveform* clock, DigitalWaveform& samples);

	//Find interpolated zero crossings of a signal
	static void FindRisingEdges(AnalogWaveform* data, float threshold, std::vector<int64_t>& edges);
	static void FindZeroCrossings(AnalogWaveform* data, float threshold, std::vector<int64_t>& edges);

	//Find edges in a signal (discarding repeated samples)
	static void FindZeroCrossings(DigitalWaveform* data, std::vector<int64_t>& edges);
	static void FindRisingEdges(DigitalWaveform* data, std::vector<int64_t>& edges);
	static void FindFallingEdges(DigitalWaveform* data, std::vector<int64_t>& edges);

	static void ClearAnalysisCache();

	//Checksum helpers
	static uint32_t CRC32(std::vector<uint8_t>& bytes, size_t start, size_t end);

protected:
	//Helpers for sampling
	static void FillDurationsGeneric(WaveformBase& wfm);
	static void FillDurationsAVX2(WaveformBase& wfm);

public:
	sigc::signal<void> signal_outputsChanged()
	{ return m_outputsChangedSignal; }

protected:
	///@brief Signal emitted when the set of output streams changes
	sigc::signal<void> m_outputsChangedSignal;

	/**
		@brief Instance number (for auto naming)

		Starts at 0 for the first filter of a given class type created, then increments
	 */
	unsigned int m_instanceNum;

protected:

#ifdef HAVE_OPENCL

	//OpenCL state
	cl::Program* m_program;
	cl::Kernel* m_kernel;

#endif

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
	virtual std::string GetProtocolDisplayName() \
	{ return GetProtocolName(); }

#define AddDecoderClass(T) Filter::DoAddDecoderClass(T::GetProtocolName(), T::CreateInstance)

#endif
