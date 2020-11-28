/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2020 Andrew D. Zonenberg                                                                          *
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
	@brief Declaration of FFTFilter
 */
#ifndef FFTFilter_h
#define FFTFilter_h

#include <ffts.h>

class FFTFilter : public PeakDetectionFilter
{
public:
	FFTFilter(const std::string& color);
	virtual ~FFTFilter();

	virtual void Refresh();

	virtual bool NeedsConfig();
	virtual bool IsOverlay();

	static std::string GetProtocolName();
	virtual void SetDefaultName();

	virtual double GetVoltageRange();
	virtual double GetOffset();
	virtual bool ValidateChannel(size_t i, StreamDescriptor stream);

	virtual void SetVoltageRange(double range);
	virtual void SetOffset(double offset);

	enum WindowFunction
	{
		WINDOW_RECTANGULAR,
		WINDOW_HANN,
		WINDOW_HAMMING,
		WINDOW_BLACKMAN_HARRIS
	};

	//Window function helpers
	static void ApplyWindow(const float* data, size_t len, float* out, WindowFunction func);
	static void HannWindow(const float* data, size_t len, float* out);
	static void HammingWindow(const float* data, size_t len, float* out);
	static void CosineSumWindow(const float* data, size_t len, float* out, float alpha0);
	static void CosineSumWindowAVX2(const float* data, size_t len, float* out, float alpha0);
	static void BlackmanHarrisWindow(const float* data, size_t len, float* out);
	static void BlackmanHarrisWindowAVX2(const float* data, size_t len, float* out);

	PROTOCOL_DECODER_INITPROC(FFTFilter)

protected:
	void NormalizeOutput(AnalogWaveform* cap, size_t nouts, size_t npoints);
	void NormalizeOutputAVX2(AnalogWaveform* cap, size_t nouts, size_t npoints);

	void ReallocateBuffers(size_t npoints_raw, size_t npoints, size_t nouts);

	size_t m_cachedNumPoints;
	float* m_rdin;
	float* m_rdout;
	ffts_plan_t* m_plan;

	float m_range;
	float m_offset;

	std::string m_windowName;
};

#endif
