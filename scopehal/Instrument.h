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
	@brief Declaration of Instrument
 */

#ifndef Instrument_h
#define Instrument_h

/**
	@brief An arbitrary lab instrument. Oscilloscope, LA, PSU, DMM, etc

	An instrument has one or more channels (theoretically zero is allowed, but this would make little sense),
	each of which may have different capabilities. For example, an oscilloscope might have four oscilloscope
	channels which can also be used as multimeter inputs, and one function/arbitrary waveform generator output,
	for a total of five channels.

	All channels regardless of type occupy a single zero-based namespace.
 */
class Instrument
{
public:
	virtual ~Instrument();

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
		INST_OSCILLOSCOPE 		= 0x01,

		//A multimeter (query to see what measurements it supports)
		INST_DMM 				= 0x02,

		//A power supply
		INST_PSU				= 0x04,

		//A function generator
		INST_FUNCTION			= 0x08,

		//An RF signal generator
		INST_RF_GEN				= 0x10
	};

	/**
		@brief Returns a bitfield describing the set of instrument types that this instrument supports.

		Not all types may be available on a given channel.
	 */
	virtual unsigned int GetInstrumentTypes() =0;

	/**
		@brief Returns a bitfield describing the set of instrument types that a given channel supports.
	 */
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) =0;

	/**
		@brief Gets the number of channels this instrument has.

		Only hardware I/O channels are included, not math/memory.
	 */
	virtual size_t GetChannelCount() =0;

	//Device information
	virtual std::string GetName() =0;
	virtual std::string GetVendor() =0;
	virtual std::string GetSerial() =0;

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
};

#endif
