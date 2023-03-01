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
	@brief Declaration of InstrumentChannel
 */

#ifndef InstrumentChannel_h
#define InstrumentChannel_h

#include "Stream.h"

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
	InstrumentChannel(const std::string& hwname, size_t i);
	virtual ~InstrumentChannel();

	virtual void SetDisplayName(std::string name);
	virtual std::string GetDisplayName();

	/**
		@brief Gets the hardware name of the channel (m_hwname)
	 */
	std::string GetHwname()
	{ return m_hwname; }

	/**
		@brief Gets the (zero based) index of the channel
	 */
	size_t GetIndex()
	{ return m_index; }

	/**
		@brief Sets the display name to an empty string, causing a fetch from hardware

		This should only be used by instrument driver implementations.
	 */
	void ClearCachedDisplayName()
	{ m_displayname = ""; }

protected:

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
};

#endif
