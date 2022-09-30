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
	@brief Declaration of Stream
 */
#ifndef Stream_h
#define Stream_h

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

	/**
		@brief General data type stored in a stream

		This type is always valid even if m_waveform is null.
	 */
	enum StreamType
	{
		//Conventional time-series waveforms (or similar graphs like a FFT)
		STREAM_TYPE_ANALOG,
		STREAM_TYPE_DIGITAL,
		STREAM_TYPE_DIGITAL_BUS,

		//2D density plots
		STREAM_TYPE_EYE,
		STREAM_TYPE_SPECTROGRAM,
		STREAM_TYPE_WATERFALL,

		//Special channels not used for display
		STREAM_TYPE_TRIGGER,	//external trigger input, doesn't have data capture

		//Class datatype from a protocol decoder
		STREAM_TYPE_PROTOCOL,

		//Other / unspecified
		STREAM_TYPE_UNDEFINED
	};

	Stream(Unit yunit, std::string name, StreamType type, uint8_t flags = 0)
	: m_yAxisUnit(yunit)
	, m_name(name)
	, m_waveform(nullptr)
	, m_stype(type)
	, m_flags(flags)
	{}

	///Unit of measurement for our vertical axis
	Unit m_yAxisUnit;

	///@brief Name of the stream
	std::string m_name;

	///@brief The current waveform (or null if nothing here)
	WaveformBase* m_waveform;

	///@brief General datatype stored in the stream
	StreamType m_stype;

	
	/**
		@brief Flags that apply to this waveform. Bitfield.
		STREAM_DO_NOT_INTERPOLATE: *hint* that this stream should not be rendered with interpolation
		                           even though/if it is analog. E.g. measurement values related to
		                           discrete parts of a waveform.
	 */
	uint8_t m_flags;

	enum
	{
		STREAM_DO_NOT_INTERPOLATE = 1
	};
};

#endif
