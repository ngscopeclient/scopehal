/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
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
#include "../scopehal/ActionProvider.h"

class ConstellationPoint
{
public:
	ConstellationPoint(int64_t x, float y, float xn, float yn)
		: m_xval(x)
		, m_yval(y)
		, m_xnorm(xn)
		, m_ynorm(yn)
	{}

	///@brief Nominal X coordinate
	int64_t m_xval;

	///@brief Nominal Y coordinate
	float m_yval;

	///@brief Normalized X coordinate
	float m_xnorm;

	///@brief Normalized Y coordinate
	float m_ynorm;
};

class ConstellationFilter
	: public Filter
	, public ActionProvider
{
public:
	ConstellationFilter(const std::string& color);

	virtual void Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue) override;
	virtual DataLocation GetInputLocation() override;

	static std::string GetProtocolName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	virtual float GetVoltageRange(size_t stream) override;
	virtual float GetOffset(size_t stream) override;

	virtual void ClearSweeps() override;

	virtual std::vector<std::string> EnumActions() override;
	virtual bool PerformAction(const std::string& id) override;

	ConstellationWaveform* ReallocateWaveform();

	void SetWidth(size_t width)
	{
		if(m_width != width)
		{
			SetData(nullptr, 0);
			m_width = width;
		}
	}

	void SetHeight(size_t height)
	{
		if(m_height != height)
		{
			SetData(nullptr, 0);
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

	enum modulation_t
	{
		MOD_NONE,
		MOD_QAM4,
		MOD_QAM9,
		MOD_QAM16,
		MOD_QAM32,
		MOD_QAM64,
		MOD_PSK8
	};

	const std::vector<ConstellationPoint>& GetNominalPoints()
	{ return m_points; }

protected:
	void RecomputeNominalPoints();
	void GetMinMaxSymbols(
		std::vector<size_t>& hist,
		float vmin,
		float& vmin_out,
		float& vmax_out,
		float binsize,
		size_t order,
		ssize_t nbins);

	size_t m_height;
	size_t m_width;

	float m_xscale;

	std::string m_modulation;
	std::string m_nomci;
	std::string m_nomcq;
	std::string m_nomr;

	double m_evmSum;
	int64_t m_evmCount;

	///@brief Nominal locations of each constellation point
	std::vector<ConstellationPoint> m_points;
};

#endif
