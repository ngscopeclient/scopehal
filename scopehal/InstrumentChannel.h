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
	@brief Declaration of InstrumentChannel
 */

#ifndef InstrumentChannel_h
#define InstrumentChannel_h

#include "Stream.h"

class Instrument;

/**
	@brief A single channel of an instrument

	A "channel" generally refers to a single physical connector on the front panel of the device, however sometimes
	multiple connectors (e.g. multimeter positive and negative probes) are logically considered one channel.

	Channels may be input or output, and may have multiple functions.

	For example, consider a mixed signal oscilloscope with multimeter and function generator option:
		* Four analog inputs, each are usable as oscilloscope or multimeter inputs
		* One trigger input
		* Sixteen logic analyzer inputs
		* One function generator output

	This instrument implements three device classes (oscilloscope, multimeter, and function generator) across a total
	of 22 channels, however no one channel supports all three APIs.

	This base class implements functionality which is common to channels from any kind of instrument
 */
class InstrumentChannel : public FlowGraphNode
{
public:
	InstrumentChannel(
		Instrument* inst,
		const std::string& hwname,
		const std::string& color = "#808080",
		Unit xunit = Unit(Unit::UNIT_FS),
		size_t index = 0);

	InstrumentChannel(
		Instrument* inst,
		const std::string& hwname,
		const std::string& color = "#808080",
		Unit xunit = Unit(Unit::UNIT_FS),
		Unit yunit = Unit(Unit::UNIT_VOLTS),
		Stream::StreamType stype = Stream::STREAM_TYPE_ANALOG,
		size_t index = 0);

	virtual ~InstrumentChannel();

	///Display color (HTML hex notation with optional alpha channel: #RRGGBB or ##RRGGBBAA)
	std::string m_displaycolor;

	virtual void SetDisplayName(std::string name);
	virtual std::string GetDisplayName();

	///@brief Gets the hardware name of the channel (m_hwname)
	std::string GetHwname()
	{ return m_hwname; }

	///@brief Gets the (zero based) index of the channel
	size_t GetIndex()
	{ return m_index; }

	///@brief Gets the instrument this channel is part of (if any)
	Instrument* GetInstrument()
	{ return m_instrument; }

	/**
		@brief Sets the display name to an empty string, causing a fetch from hardware

		This should only be used by instrument driver implementations.
	 */
	void ClearCachedDisplayName()
	{ m_displayname = ""; }

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Connector types

	enum PhysicalConnector
	{
		CONNECTOR_BANANA_DUAL,
		CONNECTOR_BMA,
		CONNECTOR_BNC,
		CONNECTOR_K,
		CONNECTOR_K_DUAL,
		CONNECTOR_N,
		CONNECTOR_SMA
	};

	virtual PhysicalConnector GetPhysicalConnector();

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Stream management
public:

	void SetData(WaveformBase* pNew, size_t stream);

	/**
		@brief Returns the X axis unit for this channel
	 */
	virtual Unit GetXAxisUnits()
	{ return m_xAxisUnit; }

	/**
		@brief Returns the Y axis unit for a specified stream
	 */
	virtual Unit GetYAxisUnits(size_t stream)
	{ return m_streams[stream].m_yAxisUnit; }

	/**
		@brief Changes the X axis unit for this channel

		This function is intended for filter/driver implementations.
		No actual conversion of data is performed, so calling this with an incorrect unit may lead to confusing results.
	 */
	virtual void SetXAxisUnits(const Unit& rhs)
	{ m_xAxisUnit = rhs; }

	/**
		@brief Changes the X axis unit for a specified stream.

		This function is intended for filter/driver implementations.
		No actual conversion of data is performed, so calling this with an incorrect unit may lead to confusing results.
	 */
	virtual void SetYAxisUnits(const Unit& rhs, size_t stream)
	{ m_streams[stream].m_yAxisUnit = rhs; }

	///@brief Returns the type of a specified stream
	Stream::StreamType GetType(size_t stream)
	{
		if(stream < m_streams.size())
			return m_streams[stream].m_stype;
		else
			return Stream::STREAM_TYPE_UNDEFINED;
	}

	///@brief Get the number of data streams
	size_t GetStreamCount()
	{ return m_streams.size(); }

	///@brief Gets the name of a stream (for display in the UI)
	std::string GetStreamName(size_t stream)
	{
		if(stream < m_streams.size())
			return m_streams[stream].m_name;
		else
			return "";
	}

	///@brief Get the contents of a data stream
	WaveformBase* GetData(size_t stream)
	{
		if(stream >= m_streams.size())
			return nullptr;
		return m_streams[stream].m_waveform;
	}

	///@brief Get the flags of a data stream
	uint8_t GetStreamFlags(size_t stream)
	{
		if(stream >= m_streams.size())
			return 0;
		return m_streams[stream].m_flags;
	}

	///@brief Gets the value of a scalar data stream
	float GetScalarValue(size_t stream)
	{
		if(stream >= m_streams.size())
			return 0;
		return m_streams[stream].m_value;
	}

	///@brief Sets the value of a scalar data stream
	void SetScalarValue(size_t stream, float value)
	{
		if(stream >= m_streams.size())
			return;
		m_streams[stream].m_value = value;
	}

	/**
		@brief Detach the capture data from this channel

		Once this function is called, the waveform is now owned by the caller and not the channel object.
	 */
	WaveformBase* Detach(size_t stream)
	{
		WaveformBase* tmp = m_streams[stream].m_waveform;
		m_streams[stream].m_waveform = NULL;
		return tmp;
	}

	/**
		@brief Determine whether the channel's waveform(s) should be persisted to a session file
	 */
	virtual bool ShouldPersistWaveform();

	/**
		@brief Selects how the channel should be displayed in e.g. the ngscopeclient filter graph editor
	 */
	enum VisibilityMode
	{
		VIS_HIDE,
		VIS_AUTO,	//decide based on whether it's enabled etc
		VIS_SHOW,
	} m_visibilityMode;

protected:

	virtual void ClearStreams();
	virtual size_t AddStream(Unit yunit, const std::string& name, Stream::StreamType stype, uint8_t flags = 0);

	///@brief The instrument we're part of (may be null in the case of filters etc)
	Instrument* m_instrument;

	/**
		@brief Hardware name of the channel

		This is normally whatever the channel is called via SCPI, so it can be directly used to build SCPI queries.
		For non-SCPI instruments, use a reasonable default name for the channel.
	 */
	std::string m_hwname;

	/**
		@brief Display name (user defined, defaults to m_hwname).

		Note that this is mostly used for filters; channels that belong to an instrument typically store the display
		name in the driver so that it can be synchronized with the instrument front panel display.
	 */
	std::string m_displayname;

	/**
		@brief Zero based index of the channel within the instrument
	 */
	size_t m_index;

	/**
		@brief Unit of measurement for our horizontal axis (common to all streams)
	 */
	Unit m_xAxisUnit;

	/**
		@brief Configuration data for each of our output streams
	 */
	std::vector<Stream> m_streams;
};

#endif
