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
	@brief Declaration of ProtocolDecoder
 */

#ifndef ProtocolDecoder_h
#define ProtocolDecoder_h

#include "OscilloscopeChannel.h"

class ProtocolDecoderParameter
{
public:
	enum ParameterTypes
	{
		TYPE_FLOAT,
		TYPE_INT,
		TYPE_BOOL,
		TYPE_FILENAME
	};

	ProtocolDecoderParameter(ParameterTypes type = TYPE_INT);

	void ParseString(std::string str);
	std::string ToString();

	bool GetBoolVal()
	{ return (m_intval != 0); }

	int GetIntVal();
	float GetFloatVal();
	std::string GetFileName();

	void SetBoolVal(bool b)
	{ m_intval = b; }
	void SetIntVal(int i);
	void SetFloatVal(float f);
	void SetFileName(std::string f);

	ParameterTypes GetType()
	{ return m_type; }

protected:
	ParameterTypes m_type;

	int m_intval;
	float m_floatval;
	std::string m_filename;
};

/**
	@brief Abstract base class for all protocol decoders
 */
class ProtocolDecoder : public OscilloscopeChannel
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

	ProtocolDecoder(OscilloscopeChannel::ChannelType type, std::string color, Category cat);
	virtual ~ProtocolDecoder();

	virtual void Refresh() =0;

	virtual void AddRef();
	virtual void Release();

	//Decoder enumeration
	static std::set<ProtocolDecoder*> EnumDecodes()
	{ return m_decodes; }

	/**
		@brief Clears any integrated data from past triggers (e.g. eye patterns).

		Most decoders shouldn't have to do anything for this.
	 */
	virtual void ClearSweeps();

	virtual void SetDefaultName() =0;

	//Channels
	size_t GetInputCount();
	std::string GetInputName(size_t i);
	void SetInput(size_t i, OscilloscopeChannel* channel);
	void SetInput(std::string name, OscilloscopeChannel* channel);

	OscilloscopeChannel* GetInput(size_t i);

	virtual bool ValidateChannel(size_t i, OscilloscopeChannel* channel) =0;

	ProtocolDecoderParameter& GetParameter(std::string s);
	typedef std::map<std::string, ProtocolDecoderParameter> ParameterMapType;
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
		@brief Standard colors for protocol decoder decode overlays.

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

	///The channels corresponding to our signals
	std::vector<OscilloscopeChannel*> m_channels;

	///Group used for the display menu
	Category m_category;

	///Indicates if our output is out-of-sync with our input
	bool m_dirty;

public:
	//Text formatting for CHANNEL_TYPE_COMPLEX decodes
	virtual Gdk::Color GetColor(int i);
	virtual std::string GetText(int i);

	//Helpers for superresolution
	static float InterpolateTime(AnalogWaveform* cap, size_t a, float voltage);

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
	virtual std::string GetTextForAsciiChannel(int i);

	//Samples a digital channel on the edges of another channel.
	//The two channels need not be the same sample rate.
	void SampleOnAnyEdges(DigitalWaveform* data, DigitalWaveform* clock, DigitalWaveform& samples);
	void SampleOnAnyEdges(DigitalBusWaveform* data, DigitalWaveform* clock, DigitalBusWaveform& samples);
	void SampleOnRisingEdges(DigitalWaveform* data, DigitalWaveform* clock, DigitalWaveform& samples);
	void SampleOnRisingEdges(DigitalBusWaveform* data, DigitalWaveform* clock, DigitalBusWaveform& samples);
	void SampleOnFallingEdges(DigitalWaveform* data, DigitalWaveform* clock, DigitalWaveform& samples);

	//Find interpolated zero crossings of a signal
	void FindZeroCrossings(AnalogWaveform* data, float threshold, std::vector<int64_t>& edges);

public:
	typedef ProtocolDecoder* (*CreateProcType)(std::string);
	static void DoAddDecoderClass(std::string name, CreateProcType proc);

	static void EnumProtocols(std::vector<std::string>& names);
	static ProtocolDecoder* CreateDecoder(std::string protocol, std::string color);

protected:
	//Class enumeration
	typedef std::map< std::string, CreateProcType > CreateMapType;
	static CreateMapType m_createprocs;

	//Decoder enumeration
	static std::set<ProtocolDecoder*> m_decodes;
};

#define PROTOCOL_DECODER_INITPROC(T) \
	static ProtocolDecoder* CreateInstance(std::string color) \
	{ \
		return new T(color); \
	} \
	virtual std::string GetProtocolDisplayName() \
	{ return GetProtocolName(); }

#define AddDecoderClass(T) ProtocolDecoder::DoAddDecoderClass(T::GetProtocolName(), T::CreateInstance)

#endif
