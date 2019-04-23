/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2019 Andrew D. Zonenberg                                                                          *
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
	@brief A single bit on a USB 1.x/2.x differential bus
 */
class USBLineSymbol
{
public:

	enum SegmentType
	{
		TYPE_J,
		TYPE_K,
		TYPE_SE0,
		TYPE_SE1
	};

	USBLineSymbol(SegmentType type = TYPE_SE1)
	 : m_type(type)
	{
	}

	SegmentType m_type;

	bool operator==(const USBLineSymbol& rhs) const
	{
		return (m_type == rhs.m_type);
	}
};

typedef OscilloscopeSample<USBLineSymbol> USBLineSample;
typedef CaptureChannel<USBLineSymbol> USBLineStateCapture;

/**
	@file
	@author Andrew D. Zonenberg
	@brief Declaration of USBLineStateDecoder
 */
#ifndef USBLineStateDecoder_h
#define USBLineStateDecoder_h

#include "../scopehal/ProtocolDecoder.h"

class USBLineStateDecoder : public ProtocolDecoder
{
public:
	USBLineStateDecoder(std::string color);

	virtual void Refresh();
	virtual ChannelRenderer* CreateRenderer();

	virtual bool NeedsConfig();
	virtual bool IsOverlay();

	static std::string GetProtocolName();
	virtual void SetDefaultName();

	virtual double GetVoltageRange();

	virtual bool ValidateChannel(size_t i, OscilloscopeChannel* channel);

	PROTOCOL_DECODER_INITPROC(USBLineStateDecoder)

protected:
	std::string m_speedname;
};

#endif
