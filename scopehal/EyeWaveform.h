/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
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
	@brief Declaration of EyeWaveform
 */

#ifndef EyeWaveform_h
#define EyeWaveform_h

#include "../scopehal/DensityFunctionWaveform.h"

class EyeWaveform : public DensityFunctionWaveform
{
public:
	enum EyeType
	{
		EYE_NORMAL,		//Eye is a normal measurement from a realtime or sampling scope
		EYE_BER,		//Eye is a SERDES BER measurement (scaled by 1e15)
	};

	EyeWaveform(size_t width, size_t height, float center, EyeWaveform::EyeType etype);
	virtual ~EyeWaveform();

	//not copyable or assignable
	EyeWaveform(const EyeWaveform&) =delete;
	EyeWaveform& operator=(const EyeWaveform&) =delete;

	int64_t* GetAccumData()
	{ return m_accumdata; }

	void Normalize();

	size_t GetTotalUIs()
	{ return m_totalUIs; }

	float GetCenterVoltage()
	{ return m_centerVoltage; }

	void IntegrateUIs(size_t uis)
	{ m_totalUIs += uis; }

	float GetUIWidth()
	{ return m_uiWidth; }

	float m_uiWidth;

	float m_saturationLevel;

	float GetMaskHitRate()
	{ return m_maskHitRate; }

	void SetMaskHitRate(float rate)
	{ m_maskHitRate = rate; }

	double GetBERAtPoint(ssize_t pointx, ssize_t pointy, ssize_t xmid, ssize_t ymid);

	EyeType GetType()
	{ return m_type; }

protected:
	int64_t* m_accumdata;

	size_t m_totalUIs;
	float m_centerVoltage;

	float m_maskHitRate;

	EyeType m_type;
};

#endif
