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

#include "scopeprotocols.h"
#include "TappedDelayLineFilter.h"
#include <immintrin.h>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

TappedDelayLineFilter::TappedDelayLineFilter(const string& color)
	: Filter(color, CAT_MATH)
	, m_tapDelayName("Tap Delay")
	, m_tap0Name("Tap Value 0")
	, m_tap1Name("Tap Value 1")
	, m_tap2Name("Tap Value 2")
	, m_tap3Name("Tap Value 3")
	, m_tap4Name("Tap Value 4")
	, m_tap5Name("Tap Value 5")
	, m_tap6Name("Tap Value 6")
	, m_tap7Name("Tap Value 7")
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput("in");

	m_parameters[m_tapDelayName] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));
	m_parameters[m_tapDelayName].SetIntVal(200000);

	m_parameters[m_tap0Name] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_tap0Name].SetFloatVal(1);

	m_parameters[m_tap1Name] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_tap1Name].SetFloatVal(0);

	m_parameters[m_tap2Name] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_tap2Name].SetFloatVal(0);

	m_parameters[m_tap3Name] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_tap3Name].SetFloatVal(0);

	m_parameters[m_tap4Name] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_tap4Name].SetFloatVal(0);

	m_parameters[m_tap5Name] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_tap5Name].SetFloatVal(0);

	m_parameters[m_tap6Name] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_tap6Name].SetFloatVal(0);

	m_parameters[m_tap7Name] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_tap7Name].SetFloatVal(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool TappedDelayLineFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string TappedDelayLineFilter::GetProtocolName()
{
	return "Tapped Delay Line";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void TappedDelayLineFilter::Refresh()
{
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din = GetAnalogInputWaveform(0);
	size_t len = din->m_samples.size();
	if(len < 8)
	{
		SetData(NULL, 0);
		return;
	}
	m_xAxisUnit = m_inputs[0].m_channel->GetXAxisUnits();
	SetYAxisUnits(m_inputs[0].GetYAxisUnits(), 0);

	//Set up output
	int64_t tap_delay = m_parameters[m_tapDelayName].GetIntVal();
	const int64_t tap_count = 8;
	int64_t samples_per_tap = tap_delay / din->m_timescale;
	auto cap = SetupOutputWaveform(din, 0, tap_count * samples_per_tap, 0);

	//Extract tap values
	float taps[8] =
	{
		m_parameters[m_tap0Name].GetFloatVal(),
		m_parameters[m_tap1Name].GetFloatVal(),
		m_parameters[m_tap2Name].GetFloatVal(),
		m_parameters[m_tap3Name].GetFloatVal(),
		m_parameters[m_tap4Name].GetFloatVal(),
		m_parameters[m_tap5Name].GetFloatVal(),
		m_parameters[m_tap6Name].GetFloatVal(),
		m_parameters[m_tap7Name].GetFloatVal()
	};

	//Run the actual filter
	DoFilterKernel(tap_delay, taps, din, cap);
}

void TappedDelayLineFilter::DoFilterKernel(
	int64_t tap_delay,
	float* taps,
	AnalogWaveform* din,
	AnalogWaveform* cap)
{
	if(g_hasAvx2)
		DoFilterKernelAVX2(tap_delay, taps, din, cap);
	else
		DoFilterKernelGeneric(tap_delay, taps, din, cap);
}

void TappedDelayLineFilter::DoFilterKernelGeneric(
	int64_t tap_delay,
	float* taps,
	AnalogWaveform* din,
	AnalogWaveform* cap)
{
	//For now, no resampling. Assume tap delay is an integer number of samples.
	int64_t samples_per_tap = tap_delay / cap->m_timescale;

	//Setup
	size_t len = din->m_samples.size();
	size_t filterlen = 8*samples_per_tap;
	size_t end = len - filterlen;

	//Do the filter
	for(size_t i=0; i<end; i++)
	{
		float v = 0;
		for(int64_t j=0; j<8; j++)
			v += din->m_samples[i + j*samples_per_tap] * taps[7 - j];
		cap->m_samples[i]	= v;
	}
}

__attribute__((target("avx2")))
void TappedDelayLineFilter::DoFilterKernelAVX2(
	int64_t tap_delay,
	float* taps,
	AnalogWaveform* din,
	AnalogWaveform* cap)
{
	//For now, no resampling. Assume tap delay is an integer number of samples.
	int64_t samples_per_tap = tap_delay / cap->m_timescale;

	//Setup
	size_t len = din->m_samples.size();
	size_t filterlen = 8*samples_per_tap;
	size_t end = len - filterlen;

	//Reverse the taps
	float taps_reversed[8] =
	{ taps[7], taps[6], taps[5], taps[4], taps[3], taps[2], taps[1], taps[0] };

	//I/O pointers
	float* pin = (float*)&din->m_samples[0];
	float* pout = (float*)&cap->m_samples[0];
	size_t end_rounded = end - (end % 8);
	size_t i=0;

	//Vector loop.
	//The filter is hard to vectorize because of striding.
	//So rather than vectorizing the inner loop, we unroll it and vectorize 8 output samples at a time.
	for(; i<end_rounded; i += 8)
	{
		//Load the first half of the inputs and coefficients
		//The first sample is guaranteed to be aligned. Subsequent ones may not be.
		//7 clock latency for all loads/broadcasts on Skylake,
		//and 2 IPC throughput.
		float* base = pin + i;
		__m256 vin0 = _mm256_load_ps(base);
		__m256 tap0 = _mm256_broadcast_ss(&taps_reversed[0]);
		__m256 vin1 = _mm256_loadu_ps(base + samples_per_tap);
		__m256 tap1 = _mm256_broadcast_ss(&taps_reversed[1]);
		__m256 vin2 = _mm256_loadu_ps(base + 2*samples_per_tap);
		__m256 tap2 = _mm256_broadcast_ss(&taps_reversed[2]);
		__m256 vin3 = _mm256_loadu_ps(base + 3*samples_per_tap);
		__m256 tap3 = _mm256_broadcast_ss(&taps_reversed[3]);

		//Calculate the results for the first half
		__m256 prod0 = _mm256_mul_ps(vin0, tap0);
		__m256 prod1 = _mm256_mul_ps(vin1, tap1);
		__m256 prod2 = _mm256_mul_ps(vin2, tap2);
		__m256 prod3 = _mm256_mul_ps(vin3, tap3);
		__m256 v01 = _mm256_add_ps(prod0, prod1);
		__m256 v23 = _mm256_add_ps(prod2, prod3);

		//Now we can load the second half and repeat the process
		__m256 vin4 = _mm256_loadu_ps(base + 4*samples_per_tap);
		__m256 tap4 = _mm256_broadcast_ss(&taps_reversed[4]);
		__m256 vin5 = _mm256_loadu_ps(base + 5*samples_per_tap);
		__m256 tap5 = _mm256_broadcast_ss(&taps_reversed[5]);
		__m256 vin6 = _mm256_loadu_ps(base + 6*samples_per_tap);
		__m256 tap6 = _mm256_broadcast_ss(&taps_reversed[6]);
		__m256 vin7 = _mm256_loadu_ps(base + 7*samples_per_tap);
		__m256 tap7 = _mm256_broadcast_ss(&taps_reversed[7]);

		//Calculate the results for the first half
		__m256 prod4 = _mm256_mul_ps(vin4, tap4);
		__m256 prod5 = _mm256_mul_ps(vin5, tap5);
		__m256 prod6 = _mm256_mul_ps(vin6, tap6);
		__m256 prod7 = _mm256_mul_ps(vin7, tap7);
		__m256 v45 = _mm256_add_ps(prod4, prod5);
		__m256 v67 = _mm256_add_ps(prod6, prod7);

		//Final summations
		__m256 v03 = _mm256_add_ps(v01, v23);
		__m256 v47 = _mm256_add_ps(v45, v67);
		__m256 sum = _mm256_add_ps(v03, v47);

		//Store the output
		_mm256_store_ps(pout + i, sum);
	}

	//Catch stragglers at the end
	for(; i<end; i++)
	{
		float v = pin[i] * taps_reversed[0];
		v += pin[i + 1*samples_per_tap] * taps_reversed[1];
		v += pin[i + 2*samples_per_tap] * taps_reversed[2];
		v += pin[i + 3*samples_per_tap] * taps_reversed[3];
		v += pin[i + 4*samples_per_tap] * taps_reversed[4];
		v += pin[i + 5*samples_per_tap] * taps_reversed[5];
		v += pin[i + 6*samples_per_tap] * taps_reversed[6];
		v += pin[i + 7*samples_per_tap] * taps_reversed[7];

		cap->m_samples[i]	= v;
	}
}
