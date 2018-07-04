/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2017 Andrew D. Zonenberg                                                                          *
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
	@brief Declaration of OscilloscopeSample
 */

#ifndef OscilloscopeSample_h
#define OscilloscopeSample_h

/**
	@brief Base class for all OscilloscopeSample specializations

	This class, and its derived classes, must not have any virtual functions.
 */
class OscilloscopeSampleBase
{
public:
	OscilloscopeSampleBase(int64_t off, int64_t dur)
	: m_offset(off)
	, m_duration(dur)
	{}

	/**
		@brief Offset from the start of the capture, in sample clock cycles.

		May not count at a constant rate depending on whether the capture is RLE compressed or not.
	 */
	int64_t m_offset;

	/**
		@brief Duration of the sample.

		Indicates how wide the sample should appear in the time graph. Samples may be directly adjacent in case of
		primitives, or have space between them for higher level protocols.
	 */
	int64_t m_duration;
};

/**
	@brief A single data point in an OscilloscopeChannel
 */
template<class T>
class OscilloscopeSample : public OscilloscopeSampleBase
{
public:
	OscilloscopeSample(int64_t off, int64_t dur, const T& sample)
	: OscilloscopeSampleBase(off, dur)
	, m_sample(sample)
	{}

	/**
		@brief The actual sample
	 */
	T m_sample;

	operator T&()
	{ return m_sample; }
};

/**
	@brief Digital sample (referenced to some arbitrary logic level)
 */
typedef OscilloscopeSample<bool> DigitalSample;

/**
	@brief Digital bus sample (referenced to some arbitrary logic level)
 */
typedef OscilloscopeSample< std::vector<bool> > DigitalBusSample;

/**
	@brief Analog sample (measured in volts)
 */
typedef OscilloscopeSample<float> AnalogSample;

/**
	@brief ASCII sample.

	Represents ASCII text sent over an arbitrary physical layer (such as RS-232).
 */
typedef OscilloscopeSample<char> AsciiSample;

/**
	@brief Byte sample.

	Represents bytewise data sent over an arbitrary physical layer (such as RS-232).
 */
typedef OscilloscopeSample<unsigned char> ByteSample;

/**
	@brief String sample.

	Output by a protocol decoder.
 */
typedef OscilloscopeSample<std::string> StringSample;

#endif
