/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
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
	@brief Declaration of EyePattern
 */

#ifndef EyePattern_h
#define EyePattern_h

#include "EyeMask.h"
#include "../scopehal/EyeWaveform.h"

class EyePattern : public Filter
{
public:
	EyePattern(const std::string& color);

	virtual void Refresh() override;

	static std::string GetProtocolName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	virtual float GetVoltageRange(size_t stream) override;
	virtual float GetOffset(size_t stream) override;

	virtual void ClearSweeps() override;

	void RecalculateUIWidth();
	EyeWaveform* ReallocateWaveform();

	void SetWidth(size_t width)
	{
		if(m_width != width)
		{
			SetData(NULL, 0);
			m_width = width;
		}
	}

	void SetHeight(size_t height)
	{
		if(m_height != height)
		{
			SetData(NULL, 0);
			m_height = height;
		}
	}

	int64_t GetXOffset()
	{ return m_xoff; }

	float GetXScale()
	{ return m_xscale; }

	size_t GetWidth() const
	{ return m_width; }

	size_t GetHeight() const
	{ return m_height; }

	const EyeMask& GetMask() const
	{ return m_mask; }

	enum ClockPolarity
	{
		CLOCK_RISING	= 1,
		CLOCK_FALLING	= 2,
		CLOCK_BOTH 		= 3	//CLOCK_RISING | CLOCK_FALLING
	};

	enum RangeMode
	{
		RANGE_AUTO		= 0,
		RANGE_FIXED		= 1
	};

	enum ClockAlignment
	{
		ALIGN_CENTER,
		ALIGN_EDGE
	};

	enum UIMode
	{
		MODE_AUTO,
		MODE_FIXED
	};

	PROTOCOL_DECODER_INITPROC(EyePattern)

protected:
	void DoMaskTest(EyeWaveform* cap);

	void SparsePackedInnerLoop(
		SparseAnalogWaveform* waveform,
		std::vector<int64_t>& clock_edges,
		int64_t* data,
		size_t wend,
		size_t cend,
		int32_t xmax,
		int32_t ymax,
		float xtimescale,
		float yscale,
		float yoff
		);

	void DensePackedInnerLoop(
		UniformAnalogWaveform* waveform,
		std::vector<int64_t>& clock_edges,
		int64_t* data,
		size_t wend,
		size_t cend,
		int32_t xmax,
		int32_t ymax,
		float xtimescale,
		float yscale,
		float yoff
		);

#ifdef __x86_64__
	void DensePackedInnerLoopAVX2(
		UniformAnalogWaveform* waveform,
		std::vector<int64_t>& clock_edges,
		int64_t* data,
		size_t wend,
		size_t cend,
		int32_t xmax,
		int32_t ymax,
		float xtimescale,
		float yscale,
		float yoff
		);
#endif

	size_t m_height;
	size_t m_width;

	int64_t m_xoff;
	float m_xscale;
	ClockAlignment m_lastClockAlign;

	std::string m_saturationName;
	std::string m_centerName;
	std::string m_maskName;
	std::string m_polarityName;
	std::string m_vmodeName;
	std::string m_rangeName;
	std::string m_clockAlignName;
	std::string m_rateModeName;
	std::string m_rateName;

	EyeMask m_mask;
};

#endif
