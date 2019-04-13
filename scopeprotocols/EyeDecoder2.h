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
	@brief Declaration of EyeDecoder2
 */

#include "../scopehal/ProtocolDecoder.h"
#include "../scopehal/CaptureChannel.h"

class EyeCapture2 : public CaptureChannelBase
{
public:
	EyeCapture2(size_t width, size_t height);
	virtual ~EyeCapture2();

	float* GetData()
	{ return m_outdata; }

	float* GetAccumData()
	{ return m_accumdata; }

	void Normalize();

protected:
	size_t m_width;
	size_t m_height;

	float* m_outdata;
	float* m_accumdata;

public:
	//Not really applicable for eye patterns, but...
	virtual size_t GetDepth() const;
	virtual int64_t GetEndTime() const;
	virtual int64_t GetSampleStart(size_t i) const;
	virtual int64_t GetSampleLen(size_t i) const;
	virtual bool EqualityTest(size_t i, size_t j) const;
	virtual bool SamplesAdjacent(size_t i, size_t j) const;
};

class EyeDecoder2 : public ProtocolDecoder
{
public:
	EyeDecoder2(std::string color);

	virtual void Refresh();
	virtual ChannelRenderer* CreateRenderer();

	virtual bool NeedsConfig();
	virtual bool IsOverlay();

	static std::string GetProtocolName();
	virtual void SetDefaultName();

	virtual bool ValidateChannel(size_t i, OscilloscopeChannel* channel);

	virtual double GetVoltageRange();

	int64_t GetUIWidth()
	{ return m_uiWidth; }

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

	size_t GetWidth()
	{ return m_width; }

	size_t GetHeight()
	{ return m_height; }

	PROTOCOL_DECODER_INITPROC(EyeDecoder2)

protected:

	size_t m_width;
	size_t m_height;

	size_t m_uiWidth;
};
