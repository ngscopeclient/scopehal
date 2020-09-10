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

/**
	@brief Descriptor for a single stream coming off a channel
 */
class StreamDescriptor
{
public:
	StreamDescriptor()
	: m_channel(NULL)
	, m_stream(0)
	{}

	StreamDescriptor(OscilloscopeChannel* channel, size_t stream)
		: m_channel(channel)
		, m_stream(stream)
	{}

	std::string GetName();

	OscilloscopeChannel* m_channel;
	size_t m_stream;

	WaveformBase* GetData()
	{ return m_channel->GetData(m_stream); }

	bool operator==(const StreamDescriptor& rhs) const
	{ return (m_channel == rhs.m_channel) && (m_stream == rhs.m_stream); }

	bool operator!=(const StreamDescriptor& rhs) const
	{ return (m_channel != rhs.m_channel) || (m_stream != rhs.m_stream); }

	bool operator<(const StreamDescriptor& rhs) const
	{
		if(m_channel < rhs.m_channel)
			return true;
		if( (m_channel == rhs.m_channel) && (m_stream < rhs.m_stream) )
			return true;

		return false;
	}
};

/**
	@brief Abstract base class for all protocol decoders
 */
class Filter : public OscilloscopeChannel
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

	Filter(OscilloscopeChannel::ChannelType type, std::string color, Category cat);
	virtual ~Filter();

	virtual void Refresh() =0;

	virtual void AddRef();
	virtual void Release();

	size_t GetRefCount()
	{ return m_refcount; }

	//Get all currently existing filters
	static std::set<Filter*> GetAllInstances()
	{ return m_filters; }

	/**
		@brief Clears any integrated data from past triggers (e.g. eye patterns).

		Most decoders shouldn't have to do anything for this.
	 */
	virtual void ClearSweeps();

	virtual void SetDefaultName() =0;

	//Channels
	size_t GetInputCount();
	std::string GetInputName(size_t i);
	void SetInput(size_t i, StreamDescriptor stream, bool force = false);
	void SetInput(std::string name, StreamDescriptor stream, bool force = false);

	StreamDescriptor GetInput(size_t i);

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) =0;

	FilterParameter& GetParameter(std::string s);
	typedef std::map<std::string, FilterParameter> ParameterMapType;
	ParameterMapType::iterator GetParamBegin()
	{ return m_parameters.begin(); }
	ParameterMapType::iterator GetParamEnd()
	{ return m_parameters.end(); }

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
	enum
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
	} standard_color;

	static Gdk::Color m_standardColors[STANDARD_COLOR_COUNT];

protected:

	///Names of signals we take as input
	std::vector<std::string> m_signalNames;

	//Parameters
	ParameterMapType m_parameters;

	///The channel (if any) connected to each of our inputs
	std::vector<StreamDescriptor> m_inputs;

	///Group used for the display menu
	Category m_category;

	///Indicates if our output is out-of-sync with our input
	bool m_dirty;

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

	/**
		@brief Gets the waveform attached to the specified input.

		This function is safe to call on a NULL input and will return NULL in that case.
	 */
	WaveformBase* GetInputWaveform(size_t i)
	{
		auto chan = m_inputs[i].m_channel;
		if(chan == NULL)
			return NULL;
		return chan->GetData(m_inputs[i].m_stream);
	}

	///Gets the analog waveform attached to the specified input
	AnalogWaveform* GetAnalogInputWaveform(size_t i)
	{ return dynamic_cast<AnalogWaveform*>(GetInputWaveform(i)); }

	///Gets the digital waveform attached to the specified input
	DigitalWaveform* GetDigitalInputWaveform(size_t i)
	{ return dynamic_cast<DigitalWaveform*>(GetInputWaveform(i)); }

	///Gets the digital bus waveform attached to the specified input
	DigitalBusWaveform* GetDigitalBusInputWaveform(size_t i)
	{ return dynamic_cast<DigitalBusWaveform*>(GetInputWaveform(i)); }

	/**
		@brief Creates and names an input signal
	 */
	void CreateInput(std::string name)
	{
		m_signalNames.push_back(name);
		m_inputs.push_back(StreamDescriptor(NULL, 0));
	}

	std::string GetInputDisplayName(size_t i);

public:
	//Text formatting for CHANNEL_TYPE_COMPLEX decodes
	virtual Gdk::Color GetColor(int i);
	virtual std::string GetText(int i);

	//Helpers for superresolution
	static float InterpolateTime(AnalogWaveform* cap, size_t a, float voltage);
	static float InterpolateValue(AnalogWaveform* cap, size_t index, float frac_ticks);

	//Helpers for more complex measurements
	//TODO: create some process for caching this so we don't waste CPU time
	static float GetMinVoltage(AnalogWaveform* cap);
	static float GetMaxVoltage(AnalogWaveform* cap);
	static float GetBaseVoltage(AnalogWaveform* cap);
	static float GetTopVoltage(AnalogWaveform* cap);
	static float GetAvgVoltage(AnalogWaveform* cap);
	static std::vector<size_t> MakeHistogram(AnalogWaveform* cap, float low, float high, size_t bins);

protected:
	//Common text formatting
	virtual std::string GetTextForAsciiChannel(int i, size_t stream);

	//Samples a digital channel on the edges of another channel.
	//The two channels need not be the same sample rate.
	void SampleOnAnyEdges(DigitalWaveform* data, DigitalWaveform* clock, DigitalWaveform& samples);
	void SampleOnAnyEdges(DigitalBusWaveform* data, DigitalWaveform* clock, DigitalBusWaveform& samples);
	void SampleOnRisingEdges(DigitalWaveform* data, DigitalWaveform* clock, DigitalWaveform& samples);
	void SampleOnRisingEdges(DigitalBusWaveform* data, DigitalWaveform* clock, DigitalBusWaveform& samples);
	void SampleOnFallingEdges(DigitalWaveform* data, DigitalWaveform* clock, DigitalWaveform& samples);

	//Find interpolated zero crossings of a signal
	void FindZeroCrossings(AnalogWaveform* data, float threshold, std::vector<int64_t>& edges);
	void FindZeroCrossings(AnalogWaveform* data, float threshold, std::vector<double>& edges);

public:
	typedef Filter* (*CreateProcType)(std::string);
	static void DoAddDecoderClass(std::string name, CreateProcType proc);

	static void EnumProtocols(std::vector<std::string>& names);
	static Filter* CreateFilter(std::string protocol, std::string color);

protected:
	//Class enumeration
	typedef std::map< std::string, CreateProcType > CreateMapType;
	static CreateMapType m_createprocs;

	//Object enumeration
	static std::set<Filter*> m_filters;
};

#define PROTOCOL_DECODER_INITPROC(T) \
	static Filter* CreateInstance(std::string color) \
	{ \
		return new T(color); \
	} \
	virtual std::string GetProtocolDisplayName() \
	{ return GetProtocolName(); }

#define AddDecoderClass(T) Filter::DoAddDecoderClass(T::GetProtocolName(), T::CreateInstance)

#endif
