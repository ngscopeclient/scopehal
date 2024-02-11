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
	@brief Declaration of FIRFilter
 */
#ifndef FIRFilter_h
#define FIRFilter_h

struct FIRFilterArgs
{
	uint32_t end;
	uint32_t filterlen;
};

/**
	@brief Performs an arbitrary FIR filter with tap delay equal to the sample rate
 */
class FIRFilter : public Filter
{
public:
	FIRFilter(const std::string& color);

	virtual void Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue) override;
	virtual DataLocation GetInputLocation() override;

	static std::string GetProtocolName();
	virtual void SetDefaultName() override;

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	PROTOCOL_DECODER_INITPROC(FIRFilter)

	void DoFilterKernel(
		vk::raii::CommandBuffer& cmdBuf,
		std::shared_ptr<QueueHandle> queue,
		UniformAnalogWaveform* din,
		UniformAnalogWaveform* cap);

	enum FilterType
	{
		FILTER_TYPE_LOWPASS,
		FILTER_TYPE_HIGHPASS,
		FILTER_TYPE_BANDPASS,
		FILTER_TYPE_NOTCH
	};

	void SetFilterType(FilterType type)
	{ m_parameters[m_filterTypeName].SetIntVal(type); }

	void SetFreqLow(float freq)
	{ m_parameters[m_freqLowName].SetFloatVal(freq); }

	void SetFreqHigh(float freq)
	{ m_parameters[m_freqHighName].SetFloatVal(freq); }

protected:

	void CalculateFilterCoefficients(float fa, float fb, float stopbandAtten, FilterType type);

	static float Bessel(float x);

	void DoFilterKernelGeneric(
		UniformAnalogWaveform* din,
		UniformAnalogWaveform* cap);

#ifdef __x86_64__
	void DoFilterKernelAVX2(
		UniformAnalogWaveform* din,
		UniformAnalogWaveform* cap);

	void DoFilterKernelAVX512F(
		UniformAnalogWaveform* din,
		UniformAnalogWaveform* cap);
#endif

	std::string m_filterTypeName;
	std::string m_filterLengthName;
	std::string m_stopbandAttenName;
	std::string m_freqLowName;
	std::string m_freqHighName;

	ComputePipeline m_computePipeline;

	AcceleratorBuffer<float> m_coefficients;
};

#endif
