/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2021 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Declaration of OscilloscopeChannel
 */

#ifndef OscilloscopeChannel_h
#define OscilloscopeChannel_h

#include "Waveform.h"

class Oscilloscope;

/**
	@brief Information associated with a single stream

	Each channel contains one or more streams, which represent a single element of a complex-valued waveform.
	For example, the waveform from an RTSA might have a stream for I and a stream for Q within a single channel.
	The waveform from a VNA might have a stream for magnitude and another for angle data on each path.
 */
class Stream
{
public:
	Stream();

	Stream(Unit yunit, std::string name)
	: m_yAxisUnit(yunit)
	, m_name(name)
	, m_waveform(nullptr)
	{}

	///Unit of measurement for our vertical axis
	Unit m_yAxisUnit;

	///@brief Name of the stream
	std::string m_name;

	///@brief The current waveform (or null if nothing here)
	WaveformBase* m_waveform;
};

/**
	@brief A single channel on the oscilloscope.

	Each time the scope is triggered a new Waveform is created with the new capture's data.
 */
class OscilloscopeChannel
{
public:
	//Oscilloscope has to be able to call AddStream()
	friend class Oscilloscope;
	friend class MockOscilloscope;

	enum ChannelType
	{
		//Conventional time-series waveforms (or similar graphs like a FFT)
		CHANNEL_TYPE_ANALOG,
		CHANNEL_TYPE_DIGITAL,

		//2D density plots
		CHANNEL_TYPE_EYE,
		CHANNEL_TYPE_SPECTROGRAM,

		//Special channels not used for display
		CHANNEL_TYPE_TRIGGER,	//external trigger input, doesn't have data capture

		//Complex datatype from a protocol decoder
		CHANNEL_TYPE_COMPLEX
	};

	OscilloscopeChannel(
		Oscilloscope* scope,
		const std::string& hwname,
		OscilloscopeChannel::ChannelType type,
		const std::string& color,
		int width = 1,
		size_t index = 0,
		bool physical = false);
	OscilloscopeChannel(
		Oscilloscope* scope,
		const std::string& hwname,
		OscilloscopeChannel::ChannelType type,
		const std::string& color,
		Unit xunit,
		Unit yunit,
		int width = 1,
		size_t index = 0,
		bool physical = false);
	virtual ~OscilloscopeChannel();

	///Display color (any valid GDK format)
	std::string m_displaycolor;

	//Stuff here is set once at init and can't be changed
	ChannelType GetType()
	{ return m_type; }

	std::string GetHwname()
	{ return m_hwname; }

	///Get the number of data streams
	size_t GetStreamCount()
	{ return m_streams.size(); }

	///Gets the name of a stream (for display in the UI)
	std::string GetStreamName(size_t stream)
	{ return m_streams[stream].m_name; }

	///Get the contents of a data stream
	WaveformBase* GetData(size_t stream)
	{
		if(stream >= m_streams.size())
			return nullptr;
		return m_streams[stream].m_waveform;
	}

	///Detach the capture data from this channel
	WaveformBase* Detach(size_t stream)
	{
		WaveformBase* tmp = m_streams[stream].m_waveform;
		m_streams[stream].m_waveform = NULL;
		return tmp;
	}

	///Set new data, overwriting the old data as appropriate
	void SetData(WaveformBase* pNew, size_t stream);

	int GetWidth()
	{ return m_width; }

	Oscilloscope* GetScope()
	{ return m_scope; }

	size_t GetIndex()
	{ return m_index; }

	size_t GetRefCount()
	{ return m_refcount; }

	void SetDisplayName(std::string name);
	std::string GetDisplayName();

	//Hardware configuration
public:
	bool IsEnabled();

	//Warning: these functions FORCE the channel to be on or off. May break other code that assumes it's on.
	void Enable();
	void Disable();

	//These functions are preferred in GUI or other environments with multiple consumers of waveform data.
	//The channel is reference counted and only turned off when all consumers have released it.
	virtual void AddRef();
	virtual void Release();

	enum CouplingType
	{
		COUPLE_DC_1M,		//1M ohm, DC coupled
		COUPLE_AC_1M,		//1M ohm, AC coupled
		COUPLE_DC_50,		//50 ohm, DC coupled
		COUPLE_AC_50,		//50 ohm, AC coupled
		COUPLE_GND,			//tie to ground
		COUPLE_SYNTHETIC	//channel is math, digital, or otherwise not a direct voltage measurement
	};

	virtual CouplingType GetCoupling();
	virtual void SetCoupling(CouplingType type);

	virtual std::vector<OscilloscopeChannel::CouplingType> GetAvailableCouplings();

	virtual double GetAttenuation();
	virtual void SetAttenuation(double atten);

	virtual int GetBandwidthLimit();
	virtual void SetBandwidthLimit(int mhz);

	virtual void SetDeskew(int64_t skew);
	virtual int64_t GetDeskew();

	bool IsPhysicalChannel()
	{ return m_physical; }

	virtual float GetVoltageRange(size_t stream);
	virtual void SetVoltageRange(float range, size_t stream);

	virtual float GetOffset(size_t stream);
	virtual void SetOffset(float offset, size_t stream);

	virtual Unit GetXAxisUnits()
	{ return m_xAxisUnit; }

	virtual Unit GetYAxisUnits(size_t stream)
	{ return m_streams[stream].m_yAxisUnit; }

	virtual void SetXAxisUnits(const Unit& rhs)
	{ m_xAxisUnit = rhs; }

	virtual void SetYAxisUnits(const Unit& rhs, size_t stream)
	{ m_streams[stream].m_yAxisUnit = rhs; }

	void SetDigitalHysteresis(float level);
	void SetDigitalThreshold(float level);

	void SetCenterFrequency(int64_t freq);

	bool CanAutoZero();
	void AutoZero();
	std::string GetProbeName();

	virtual bool CanInvert();
	virtual void Invert(bool invert);
	virtual bool IsInverted();

	virtual void SetInputMux(size_t select);

	void SetDefaultDisplayName();
protected:
	void SharedCtorInit(Unit unit);

	/**
		@brief Clears out any existing streams
	 */
	void ClearStreams()
	{ m_streams.clear(); }

	/**
		@brief Adds a new data stream to the channel
	 */
	void AddStream(Unit yunit, const std::string& name)
	{ m_streams.push_back(Stream(yunit, name)); }

	/**
		@brief Display name (user defined, defaults to m_hwname)

		This is ONLY used if m_scope is NULL.
	 */
	std::string m_displayname;

	/**
		@brief The oscilloscope (if any) we are part of.

		Note that filters and other special channels are not attached to a scope.
	 */
	Oscilloscope* m_scope;

	///Channel type
	ChannelType m_type;

	///Hardware name as labeled on the scope
	std::string m_hwname;

	///Bus width (1 to N, only meaningful for digital channels)
	int m_width;

	///Channel index
	size_t m_index;

	///true if this is a real physical input on the scope and not a math or other output
	bool m_physical;

	///Number of references (channel is disabled when last ref is released)
	size_t m_refcount;

	///Unit of measurement for our horizontal axis
	Unit m_xAxisUnit;

	///Stream configuration
	std::vector<Stream> m_streams;
};

#endif
