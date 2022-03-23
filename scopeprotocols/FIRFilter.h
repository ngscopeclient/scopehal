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

/**
	@brief Performs an arbitrary FIR filter with tap delay equal to the sample rate
 */
class FIRFilter : public Filter
{
public:
	FIRFilter(const std::string& color);

	virtual void Refresh();

	virtual bool NeedsConfig();

	virtual void ClearSweeps();

	static std::string GetProtocolName();
	virtual void SetDefaultName();

	virtual float GetVoltageRange(size_t stream);
	virtual float GetOffset(size_t stream);

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream);

	PROTOCOL_DECODER_INITPROC(FIRFilter)

	void DoFilterKernel(
		std::vector<float>& coefficients,
		AnalogWaveform* din,
		AnalogWaveform* cap,
		float& vmin,
		float& vmax);

	enum FilterType
	{
		FILTER_TYPE_LOWPASS,
		FILTER_TYPE_HIGHPASS,
		FILTER_TYPE_BANDPASS,
		FILTER_TYPE_NOTCH
	};

protected:

	static void CalculateFilterCoefficients(
		std::vector<float>& coefficients,
		float fa,
		float fb,
		float stopbandAtten,
		FilterType type);

	static float Bessel(float x);

	void DoFilterKernelGeneric(
		std::vector<float>& coefficients,
		AnalogWaveform* din,
		AnalogWaveform* cap,
		float& vmin,
		float& vmax);

#ifdef HAVE_OPENCL
	void DoFilterKernelOpenCL(
		std::vector<float>& coefficients,
		AnalogWaveform* din,
		AnalogWaveform* cap,
		float& vmin,
		float& vmax);
#endif

	void DoFilterKernelAVX2(
		std::vector<float>& coefficients,
		AnalogWaveform* din,
		AnalogWaveform* cap,
		float& vmin,
		float& vmax);

	void DoFilterKernelAVX512F(
		std::vector<float>& coefficients,
		AnalogWaveform* din,
		AnalogWaveform* cap,
		float& vmin,
		float& vmax);

	float m_min;
	float m_max;
	float m_range;
	float m_offset;

	std::string m_filterTypeName;
	std::string m_filterLengthName;
	std::string m_stopbandAttenName;
	std::string m_freqLowName;
	std::string m_freqHighName;
};

#endif
