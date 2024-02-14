/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
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
	@brief Declaration of ConstellationFilter
 */

#ifndef ConstellationFilter_h
#define ConstellationFilter_h

#include "../scopehal/ConstellationWaveform.h"

class ConstellationFilter : public Filter
{
public:
	ConstellationFilter(const std::string& color);

	virtual void Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue) override;

	static std::string GetProtocolName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	virtual float GetVoltageRange(size_t stream) override;
	virtual float GetOffset(size_t stream) override;

	virtual void ClearSweeps() override;

	ConstellationWaveform* ReallocateWaveform();

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
	{ return 0; }

	float GetXScale()
	{ return m_xscale; }

	size_t GetWidth() const
	{ return m_width; }

	size_t GetHeight() const
	{ return m_height; }

	PROTOCOL_DECODER_INITPROC(ConstellationFilter)

protected:
	void DoMaskTest(EyeWaveform* cap);

	size_t m_height;
	size_t m_width;

	float m_xscale;
};

#endif
