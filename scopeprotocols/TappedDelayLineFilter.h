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
	@brief Declaration of TappedDelayLineFilter
 */
#ifndef TappedDelayLineFilter_h
#define TappedDelayLineFilter_h

/**
	@brief Performs an 8-tap FIR filter with a multi-sample delay between taps.

	The delay must be an integer multiple of the sampling period.
 */
class TappedDelayLineFilter : public Filter
{
public:
	TappedDelayLineFilter(const std::string& color);

	virtual void Refresh() override;

	static std::string GetProtocolName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	PROTOCOL_DECODER_INITPROC(TappedDelayLineFilter)

	static void DoFilterKernel(
		int64_t tap_delay,
		float* taps,
		UniformAnalogWaveform* din,
		UniformAnalogWaveform* cap);

protected:

	static void DoFilterKernelGeneric(
		int64_t tap_delay,
		float* taps,
		UniformAnalogWaveform* din,
		UniformAnalogWaveform* cap);

#ifdef __x86_64__
	static void DoFilterKernelAVX2(
		int64_t tap_delay,
		float* taps,
		UniformAnalogWaveform* din,
		UniformAnalogWaveform* cap);
#endif

	std::string m_tapDelayName;
	std::string m_tap0Name;
	std::string m_tap1Name;
	std::string m_tap2Name;
	std::string m_tap3Name;
	std::string m_tap4Name;
	std::string m_tap5Name;
	std::string m_tap6Name;
	std::string m_tap7Name;
};

#endif
