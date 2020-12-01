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
	@brief Abstract base class for all filters and protocol decoders
 */
class Filter	: public OscilloscopeChannel
				, public FlowGraphNode
{
public:

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
		CAT_RF				//Frequency domain analysis (FFT etc) and other RF stuff
	};

	Filter(OscilloscopeChannel::ChannelType type, const std::string& color, Category cat);
	virtual ~Filter();

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

	virtual bool IsScalarOutput();

	virtual void Refresh() =0;

	virtual void AddRef();
	virtual void Release();

	size_t GetRefCount()
	{ return m_refcount; }

	//Get all currently existing filters
	static std::set<Filter*> GetAllInstances()
	{ return m_filters; }

	//Set all currently existing filters to the dirty state
	static void SetAllFiltersDirty()
	{
		for(auto f : m_filters)
			f->SetDirty();
	}

	/**
		@brief Clears any integrated data from past triggers (e.g. eye patterns).

		Most decoders shouldn't have to do anything for this.
	 */
	virtual void ClearSweeps();

	virtual void SetDefaultName() =0;

	Category GetCategory()
	{ return m_category; }

	/**
		@brief Return true (default) if this decoder should be overlaid on top of the original waveform.

		Return false (override) if it should be rendered as its own line.
	 */
	virtual bool IsOverlay();

	virtual bool NeedsConfig() =0;	//false if we can automatically do the decode from the signal w/ no configuration

	void RefreshIfDirty();
	void RefreshInputsIfDirty();

	void SetDirty()
	{ m_dirty = true; }

	/**
		@brief Gets the display name of this protocol (for use in menus, save files, etc). Must be unique.
	 */
	virtual std::string GetProtocolDisplayName() =0;

	/**
		@brief Serialize this decoder's configuration to a string
	 */
	virtual std::string SerializeConfiguration(IDTable& table);

	/**
		@brief Load configuration from a save file
	 */
	virtual void LoadParameters(const YAML::Node& node, IDTable& table);
	virtual void LoadInputs(const YAML::Node& node, IDTable& table);

	/**
		@brief Standard colors for protocol decode overlays.

		Do not change ordering, add new items to the end only.
	 */
	enum FilterColor
	{
		COLOR_DATA,			//protocol data
		COLOR_CONTROL,		//generic control sequences
		COLOR_ADDRESS,		//addresses or device IDs
		COLOR_PREAMBLE,		//preambles, start bits, and other constant framing
		COLOR_CHECKSUM_OK,	//valid CRC/checksum
		COLOR_CHECKSUM_BAD,	//invalid CRC/checksum
		COLOR_ERROR,		//malformed traffic
		COLOR_IDLE,			//downtime between frames

		STANDARD_COLOR_COUNT
	};

	static Gdk::Color m_standardColors[STANDARD_COLOR_COUNT];

protected:

	///Group used for the display menu
	Category m_category;

	///Indicates if our output is out-of-sync with our input
	bool m_dirty;

	///Indicates we're using an auto-generated name
	bool m_usingDefault;

	bool VerifyAllInputsOK(bool allowEmpty = false);
	bool VerifyInputOK(size_t i, bool allowEmpty = false);
	bool VerifyAllInputsOKAndAnalog();

	///Gets the timestamp of the next event (if any) on a waveform
	int64_t GetNextEventTimestamp(WaveformBase* wfm, size_t i, size_t len, int64_t timestamp)
	{
		if(i+1 < len)
			return wfm->m_offsets[i+1];
		else
			return timestamp;
	}

	///Advance the waveform to a given timestamp
	void AdvanceToTimestamp(WaveformBase* wfm, size_t& i, size_t len, int64_t timestamp)
	{
		while( ((i+1) < len) && (wfm->m_offsets[i+1] <= timestamp) )
			i ++;
	}

public:
	//Text formatting for CHANNEL_TYPE_COMPLEX decodes
	virtual Gdk::Color GetColor(int i);
	virtual std::string GetText(int i);

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

	//Samples a digital channel on the edges of another channel.
	//The two channels need not be the same sample rate.
	static void SampleOnAnyEdges(DigitalWaveform* data, DigitalWaveform* clock, DigitalWaveform& samples);
	static void SampleOnAnyEdges(DigitalBusWaveform* data, DigitalWaveform* clock, DigitalBusWaveform& samples);
	static void SampleOnRisingEdges(DigitalWaveform* data, DigitalWaveform* clock, DigitalWaveform& samples);
	static void SampleOnRisingEdges(DigitalBusWaveform* data, DigitalWaveform* clock, DigitalBusWaveform& samples);
	static void SampleOnFallingEdges(DigitalWaveform* data, DigitalWaveform* clock, DigitalWaveform& samples);

	//Find interpolated zero crossings of a signal
	static void FindZeroCrossings(AnalogWaveform* data, float threshold, std::vector<int64_t>& edges);

	//Find edges in a signal (discarding repeated samples)
	static void FindZeroCrossings(DigitalWaveform* data, std::vector<int64_t>& edges);
	static void FindRisingEdges(DigitalWaveform* data, std::vector<int64_t>& edges);
	static void FindFallingEdges(DigitalWaveform* data, std::vector<int64_t>& edges);

	static void ClearAnalysisCache();

protected:
	//Common text formatting
	virtual std::string GetTextForAsciiChannel(int i, size_t stream);

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
