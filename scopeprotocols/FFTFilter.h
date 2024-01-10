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
	@brief Declaration of FFTFilter
 */
#ifndef FFTFilter_h
#define FFTFilter_h

#ifndef _APPLE_SILICON
#include <ffts.h>
#endif

#include "VulkanFFTPlan.h"

class QueueHandle;

struct WindowFunctionArgs
{
	uint32_t numActualSamples;
	uint32_t npoints;
	uint32_t offsetIn;
	uint32_t offsetOut;
	float scale;
	float alpha0;
	float alpha1;
};

struct ComplexToMagnitudeArgs
{
	uint32_t npoints;
	float scale;
};

class FFTFilter : public PeakDetectionFilter
{
public:
	FFTFilter(const std::string& color);
	virtual ~FFTFilter();

	virtual void Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue);
	virtual DataLocation GetInputLocation();

	static std::string GetProtocolName();

	virtual float GetVoltageRange(size_t stream);
	virtual float GetOffset(size_t stream);
	virtual bool ValidateChannel(size_t i, StreamDescriptor stream);

	virtual void SetVoltageRange(float range, size_t stream);
	virtual void SetOffset(float offset, size_t stream);

	enum WindowFunction
	{
		WINDOW_RECTANGULAR,
		WINDOW_HANN,
		WINDOW_HAMMING,
		WINDOW_BLACKMAN_HARRIS
	};

	enum RoundingMode
	{
		ROUND_TRUNCATE,
		ROUND_ZERO_PAD
	};

	//Window function helpers
	static void ApplyWindow(const float* data, size_t len, float* out, WindowFunction func);
	static void HannWindow(const float* data, size_t len, float* out);
	static void HammingWindow(const float* data, size_t len, float* out);
	static void CosineSumWindow(const float* data, size_t len, float* out, float alpha0);
	static void BlackmanHarrisWindow(const float* data, size_t len, float* out);

#ifdef __x86_64__
	static void CosineSumWindowAVX2(const float* data, size_t len, float* out, float alpha0);
	static void BlackmanHarrisWindowAVX2(const float* data, size_t len, float* out);
#endif

	PROTOCOL_DECODER_INITPROC(FFTFilter)

	void SetWindowFunction(WindowFunction f)
	{ m_parameters[m_windowName].SetIntVal(f); }

protected:
	void NormalizeOutputLog(AcceleratorBuffer<float>& data, size_t nouts, float scale);
	void NormalizeOutputLogAVX2FMA(AcceleratorBuffer<float>& data, size_t nouts, float scale);
	void NormalizeOutputLinear(AcceleratorBuffer<float>& data, size_t nouts, float scale);
	void NormalizeOutputLinearAVX2(AcceleratorBuffer<float>& data, size_t nouts, float scale);

	void ReallocateBuffers(size_t npoints_raw, size_t npoints, size_t nouts);

	void DoRefresh(
		WaveformBase* din,
		AcceleratorBuffer<float>& data,
		double fs_per_sample,
		size_t npoints,
		size_t nouts,
		bool log_output,
		vk::raii::CommandBuffer& cmdBuf,
		std::shared_ptr<QueueHandle> queue
		);

	size_t m_cachedNumPoints;
	size_t m_cachedNumPointsFFT;
	AcceleratorBuffer<float> m_rdinbuf;
	AcceleratorBuffer<float> m_rdoutbuf;
#ifndef _APPLE_SILICON
	ffts_plan_t* m_plan;
#endif

	float m_range;
	float m_offset;

	std::string m_windowName;
	std::string m_roundingName;

	std::unique_ptr<VulkanFFTPlan> m_vkPlan;

	ComputePipeline m_blackmanHarrisComputePipeline;
	ComputePipeline m_rectangularComputePipeline;
	ComputePipeline m_cosineSumComputePipeline;
	ComputePipeline m_complexToMagnitudeComputePipeline;
	ComputePipeline m_complexToLogMagnitudeComputePipeline;

	bool m_cachedGpuFilterEnabled;
};

#endif
