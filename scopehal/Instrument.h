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
	@brief Declaration of Instrument
 */

#ifndef Instrument_h
#define Instrument_h

#include "InstrumentChannel.h"
#include "ConfigWarningList.h"


/**
	@brief An arbitrary lab instrument. Oscilloscope, LA, PSU, DMM, etc

	An instrument has one or more channels (theoretically zero is allowed, but this would make little sense),
	each of which may have different capabilities. For example, an oscilloscope might have four oscilloscope
	channels which can also be used as multimeter inputs, and one function/arbitrary waveform generator output,
	for a total of five channels.

	Math, memory, and other non-acquisition channels are generally not exposed in the API unless they provide features
	which are not possible to implement clientside.

	All channels regardless of type occupy a single zero-based namespace.
 */
class Instrument
	: public std::enable_shared_from_this<Instrument>
{
public:
	virtual ~Instrument();

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Instrument identification

	/*
		@brief Types of instrument.

		Note that we can't use RTTI for this because of software options that may or may not be present,
		and we don't know at object instantiation time.

		For example, some WaveSurfer 3000 devices have the function generator option and others don't.
		While the WaveSurfer 3000 DMM option is now no-cost, there's no guarantee any given instrument's
		owner has installed it!
	 */
	enum InstrumentTypes
	{
		//An oscilloscope or logic analyzer
		INST_OSCILLOSCOPE 		=  0x01,

		//A multimeter (query to see what measurements it supports)
		INST_DMM 				=  0x02,

		//A power supply
		INST_PSU				=  0x04,

		//A function generator
		INST_FUNCTION			=  0x08,

		//An RF signal generator
		INST_RF_GEN				=  0x10,

		//An electronic load
		INST_LOAD				=  0x20,

		//A bit error rate tester
		INST_BERT				=  0x40,

		//A miscellaneous instrument that doesn't fit any other category
		INST_MISC				=  0x80,

		//A switch matrix
		INST_SWITCH_MATRIX		= 0x100
	};

	/**
		@brief Returns a bitfield describing the set of instrument types that this instrument supports.

		Not all types may be available on a given channel.
	 */
	virtual unsigned int GetInstrumentTypes() const =0;

	//Device information
	virtual std::string GetName() const =0;
	virtual std::string GetVendor() const =0;
	virtual std::string GetSerial() const =0;

	/**
		@brief Optional user-selected nickname of the instrument

		(for display purposes if multiple similar devices are in use)
	 */
	std::string m_nickname;

	/**
		@brief Gets the connection string for our transport
	 */
	virtual std::string GetTransportConnectionString() =0;

	/**
		@brief Gets the name of our transport
	 */
	virtual std::string GetTransportName() =0;

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Channel enumeration and identification

	/**
		@brief Returns a bitfield describing the set of instrument types that a given channel supports.

		@param i	Channel index
	 */
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) const =0;

	/**
		@brief Gets the number of channels (of any type) this instrument has.
	 */
	size_t GetChannelCount() const
	{ return m_channels.size(); }

	/**
		@brief Gets a given channel on the instrument

		Derived classes typically implement a GetXChannel() helper function which casts the channel to the requested
		type.

		@param i		Channel index
	 */
	InstrumentChannel* GetChannel(size_t i) const
	{
		if(i >= m_channels.size())
			return nullptr;

		return m_channels[i];
	}

	/**
		@brief Gets the hardware display name for a channel. This is an arbitrary user-selected string.

		Some instruments allow displaying channel names in the GUI or on probes. If this is supported, the
		driver should override this function.

		This function does not implement any caching, so calling it directly in performance critical code is
		not advisable. Instead, call InstrumentChannel::GetDisplayName(), which caches clientside and calls this
		function only on a cache miss.

		The default implementation is a no-op.

		@param i Zero-based index of channel
	 */
	virtual std::string GetChannelDisplayName(size_t i);

	/**
		@brief Sets the hardware display name for a channel. This is an arbitrary user-selected string.

		Some instruments allow displaying channel names in the GUI or on probes. If this is supported, the
		driver should override this function.

		This function directly updates hardware without caching. In most cases, you should call
		InstrumentChannel::SetDisplayName(), which updates the clientside cache and then calls this function.

		The default function returns the hardware name.

		@param i	Zero-based index of channel
		@param name	Name of the channel
	 */
	virtual void SetChannelDisplayName(size_t i, std::string name);

	InstrumentChannel* GetChannelByDisplayName(const std::string& name);
	InstrumentChannel* GetChannelByHwName(const std::string& name);

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Data capture

	/**
		@brief Pull data from the instrument

		@return True if waveform was acquired, false if connection lost or other serious error
	 */
	virtual bool AcquireData() =0;

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Serialization

public:

	/**
		@brief Serializes this instrument's configuration to a YAML node.

		@return YAML block with this instrument's configuration
	 */
	virtual YAML::Node SerializeConfiguration(IDTable& table) const;

	/**
		@brief Load instrument and channel configuration from a save file
	 */
	virtual void LoadConfiguration(int version, const YAML::Node& node, IDTable& idmap);

	/**
		@brief Parse a limited subset of instrument configuration but do *not* apply it.

		This is an optional method intended to be called prior to loading a file in order to identify potential problems
		with the setup being loaded (for example, attenuation or output voltage settings wildly different from the
		current configuration).
	 */
	virtual void PreLoadConfiguration(int version, const YAML::Node& node, IDTable& idmap, ConfigWarningList& warnings);

protected:

	/**
		@brief List of methods which need to be called to serialize this node's configuration
	 */
	std::list< sigc::slot<void(YAML::Node&, IDTable&)> > m_serializers;

	/**
		@brief List of methods which need to be called to deserialize this node's configuration
	 */
	std::list< sigc::slot<void(int, const YAML::Node&, IDTable&)> > m_loaders;

	/**
		@brief List of methods which need to be called to pre-load this node's configuration
	 */
	std::list< sigc::slot<void(int, const YAML::Node&, IDTable&, ConfigWarningList&)> > m_preloaders;

protected:

	/**
		@brief Set of all channels on this instrument
	 */
	std::vector<InstrumentChannel*> m_channels;

};

#endif
