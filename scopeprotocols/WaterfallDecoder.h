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
	@file
	@author Andrew D. Zonenberg
	@brief Declaration of WaterfallDecoder
 */
#ifndef WaterfallDecoder_h
#define WaterfallDecoder_h

#include "../scopehal/ProtocolDecoder.h"

class WaterfallCapture : public CaptureChannelBase
{
public:
	WaterfallCapture(size_t width, size_t height);
	virtual ~WaterfallCapture();

	float* GetData()
	{ return m_outdata; }

protected:
	size_t m_width;
	size_t m_height;

	float* m_outdata;

public:
	//Not really applicable for waterfall plots
	virtual size_t GetDepth() const;
	virtual int64_t GetEndTime() const;
	virtual int64_t GetSampleStart(size_t i) const;
	virtual int64_t GetSampleLen(size_t i) const;
	virtual bool EqualityTest(size_t i, size_t j) const;
	virtual bool SamplesAdjacent(size_t i, size_t j) const;
};

class WaterfallDecoder : public ProtocolDecoder
{
public:
	WaterfallDecoder(std::string color);

	virtual void Refresh();
	virtual ChannelRenderer* CreateRenderer();

	virtual bool NeedsConfig();
	virtual bool IsOverlay();

	static std::string GetProtocolName();
	virtual void SetDefaultName();

	virtual double GetVoltageRange();
	virtual double GetOffset();
	virtual bool ValidateChannel(size_t i, OscilloscopeChannel* channel);

	void SetWidth(size_t width)
	{
		m_width = width;
		SetData(NULL);
	}

	void SetHeight(size_t height)
	{
		m_height = height;
		SetData(NULL);
	}

	void SetTimeScale(double pixelsPerHz)
	{ m_pixelsPerHz = pixelsPerHz; }

	size_t GetWidth()
	{ return m_width; }

	size_t GetHeight()
	{ return m_height; }

	PROTOCOL_DECODER_INITPROC(WaterfallDecoder)

protected:
	double m_pixelsPerHz;

	size_t m_width;
	size_t m_height;
};

#endif
