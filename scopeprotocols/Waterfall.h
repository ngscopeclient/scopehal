/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
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
	@brief Declaration of Waterfall
 */
#ifndef Waterfall_h
#define Waterfall_h

class WaterfallWaveform : public WaveformBase
{
public:
	WaterfallWaveform(size_t width, size_t height);
	virtual ~WaterfallWaveform();

	//not copyable or assignable
	WaterfallWaveform(const WaterfallWaveform&) =delete;
	WaterfallWaveform& operator=(const WaterfallWaveform&) =delete;

	float* GetData()
	{ return m_outdata; }

protected:
	size_t m_width;
	size_t m_height;

	float* m_outdata;
};

class Waterfall : public Filter
{
public:
	Waterfall(const std::string& color);

	//not copyable or assignable
	Waterfall(const Waterfall&) =delete;
	Waterfall& operator=(const Waterfall&) =delete;

	virtual void Refresh();

	virtual bool NeedsConfig();

	static std::string GetProtocolName();
	virtual void SetDefaultName();

	virtual float GetVoltageRange(size_t stream);
	virtual float GetOffset(size_t stream);
	virtual bool ValidateChannel(size_t i, StreamDescriptor stream);

	void SetWidth(size_t width)
	{
		m_width = width;
		SetData(NULL, 0);
	}

	void SetHeight(size_t height)
	{
		m_height = height;
		SetData(NULL, 0);
	}

	void SetTimeScale(double pixelsPerHz)
	{ m_pixelsPerHz = pixelsPerHz; }

	void SetTimeOffset(double offsetHz)
	{ m_offsetHz = offsetHz; }

	size_t GetWidth()
	{ return m_width; }

	size_t GetHeight()
	{ return m_height; }

	PROTOCOL_DECODER_INITPROC(Waterfall)

protected:
	double m_pixelsPerHz;
	double m_offsetHz;

	size_t m_width;
	size_t m_height;
};

#endif
