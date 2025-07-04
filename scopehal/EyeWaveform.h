/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2025 Andrew D. Zonenberg and contributors                                                         *
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
	@ingroup datamodel
 */

#ifndef EyeWaveform_h
#define EyeWaveform_h

#include "../scopehal/DensityFunctionWaveform.h"

#define EYE_ACCUM_SCALE 64

/**
	@brief An eye-pattern waveform
	@ingroup datamodel

	May be generated by a filter or directly measured by a BERT, sampling oscilloscope, etc.

	The internal data is integrated as int64 to avoid loss of precision, then normalized to float32 by Normalize()
	after being updated.

	The raw accumulator buffer is scaled by EYE_ACCUM_SCALE to enable antialiasing where a sample fits between two
	rows of samples. In other words, a single sample will produce a total of EYE_ACCUM_SCALE counts in the buffer,
	often split between several pixel locations.
 */
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

	///@brief Returns a pointer to the raw (not normalized) accumulator data
	int64_t* GetAccumData()
	{ return m_accumdata; }

	void Normalize();

	///@brief Get the total number of UIs integrated in this eye
	size_t GetTotalUIs()
	{ return m_totalUIs; }

	///@brief Get the total number of samples integrated in this eye
	size_t GetTotalSamples()
	{ return m_totalSamples; }

	/**
		@brief Get the center voltage of the eye plot (not the center of the opening)

		This is normally 0 for AC coupled links but can be nonzero if there is a DC bias.
	 */
	float GetCenterVoltage()
	{ return m_centerVoltage; }

	/**
		@brief Marks a given number of UIs as integrated.

		This does not actually do anything to waveform data, it just increments the symbol count.

		Typically called by filters at the end of a refresh cycle.

		@param uis		Number of UIs integrated
		@param samples	Number of samples integrated
	 */
	void IntegrateUIs(size_t uis, size_t samples)
	{
		m_totalUIs += uis;
		m_totalSamples += samples;
	}

	///@brief Return the UI width, in X axis units
	float GetUIWidth()
	{ return m_uiWidth; }

	/**
		@brief Nominal unit interval width of the eye.

		The entire displayed eye is two UIs wide.
	 */
	float m_uiWidth;

	/**
		@brief Saturation level for normalization

		Saturation level of 1.0 means mapping all values to [0, 1].
		2.0 means mapping values to [0, 2] and saturating anything above 1.
	 */
	float m_saturationLevel;

	///@brief Return the mask hit rate, or zero if there is no mask defined
	float GetMaskHitRate()
	{ return m_maskHitRate; }

	/**
		@brief Set the mask hit rate (normally called by the filter or instrument owning the waveform)

		@param rate	Hit rate
	 */
	void SetMaskHitRate(float rate)
	{ m_maskHitRate = rate; }

	double GetBERAtPoint(ssize_t pointx, ssize_t pointy, ssize_t xmid, ssize_t ymid);

	///@brief Return the eye type (normal or BER)
	EyeType GetType()
	{ return m_type; }

	virtual void FreeGpuMemory() override
	{}

	virtual bool HasGpuBuffer() override
	{ return false; }

protected:

	///@brief Accumulator buffer
	int64_t* m_accumdata;

	///@brief Total UIs integrated
	size_t m_totalUIs;

	///@brief Total samples integrated
	size_t m_totalSamples;

	///@brief Voltage of the vertical midpoint of the plot
	float m_centerVoltage;

	///@brief Mask hit rate
	float m_maskHitRate;

	///@brief Type of the eye pattern
	EyeType m_type;
};

#endif
