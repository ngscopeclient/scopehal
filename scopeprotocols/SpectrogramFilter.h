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

#ifndef _APPLE_SILICON

/**
	@file
	@author Andrew D. Zonenberg
	@brief Declaration of SpectrogramFilter
 */
#ifndef SpectrogramFilter_h
#define SpectrogramFilter_h

#include <ffts.h>

#include "../scopehal/DensityFunctionWaveform.h"

class SpectrogramWaveform : public DensityFunctionWaveform
{
public:
	SpectrogramWaveform(size_t width, size_t height, double binsize);
	virtual ~SpectrogramWaveform();

	//not copyable or assignable
	SpectrogramWaveform(const SpectrogramWaveform&) =delete;
	SpectrogramWaveform& operator=(const SpectrogramWaveform&) =delete;

	double GetBinSize()
	{ return m_binsize; }

protected:
	double m_binsize;
};

class SpectrogramFilter : public Filter
{
public:
	SpectrogramFilter(const std::string& color);
	virtual ~SpectrogramFilter();

	virtual void Refresh();

	static std::string GetProtocolName();

	virtual float GetVoltageRange(size_t stream);
	virtual float GetOffset(size_t stream);
	virtual bool ValidateChannel(size_t i, StreamDescriptor stream);

	virtual void SetVoltageRange(float range, size_t stream);
	virtual void SetOffset(float offset, size_t stream);

	PROTOCOL_DECODER_INITPROC(SpectrogramFilter)

protected:
	void ReallocateBuffers(size_t fftlen);

	std::vector<float, AlignedAllocator<float, 64> > m_rdinbuf;
	std::vector<float, AlignedAllocator<float, 64> > m_rdoutbuf;

	void ProcessSpectrumGeneric(
		size_t nblocks,
		size_t block,
		size_t nouts,
		float minscale,
		float range,
		float scale,
		float* data);

#ifdef __x86_64__
	void ProcessSpectrumAVX2FMA(
		size_t nblocks,
		size_t block,
		size_t nouts,
		float minscale,
		float range,
		float scale,
		float* data);
#endif

	size_t m_cachedFFTLength;

	ffts_plan_t* m_plan;
	float m_range;
	float m_offset;

	std::string m_windowName;
	std::string m_fftLengthName;
	std::string m_rangeMinName;
	std::string m_rangeMaxName;
};

#endif

#endif
