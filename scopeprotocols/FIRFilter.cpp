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

#include "scopeprotocols.h"
#include "FIRFilter.h"
#include <immintrin.h>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

FIRFilter::FIRFilter(const string& color)
	: Filter(
		OscilloscopeChannel::CHANNEL_TYPE_ANALOG,
		color,
		CAT_MATH,
		"kernels/FIRFilter.cl",
		"FIRFilter")
	, m_filterTypeName("Filter Type")
	, m_filterLengthName("Length")
	, m_stopbandAttenName("Stopband Attenuation")
	, m_freqLowName("Frequency Low")
	, m_freqHighName("Frequency High")
{
	CreateInput("in");

	m_range = 1;
	m_offset = 0;
	m_min = FLT_MAX;
	m_max = -FLT_MAX;

	m_parameters[m_filterTypeName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_filterTypeName].AddEnumValue("Low pass", FILTER_TYPE_LOWPASS);
	m_parameters[m_filterTypeName].AddEnumValue("High pass", FILTER_TYPE_HIGHPASS);
	m_parameters[m_filterTypeName].AddEnumValue("Band pass", FILTER_TYPE_BANDPASS);
	m_parameters[m_filterTypeName].AddEnumValue("Notch", FILTER_TYPE_NOTCH);
	m_parameters[m_filterTypeName].SetIntVal(FILTER_TYPE_LOWPASS);

	m_parameters[m_filterLengthName] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLEDEPTH));
	m_parameters[m_filterLengthName].SetIntVal(0);

	m_parameters[m_stopbandAttenName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_DB));
	m_parameters[m_stopbandAttenName].SetFloatVal(60);

	m_parameters[m_freqLowName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ));
	m_parameters[m_freqLowName].SetFloatVal(0);

	m_parameters[m_freqHighName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ));
	m_parameters[m_freqHighName].SetFloatVal(100e6);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool FIRFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void FIRFilter::ClearSweeps()
{
	m_range = 1;
	m_offset = 0;
	m_min = FLT_MAX;
	m_max = -FLT_MAX;
}

void FIRFilter::SetDefaultName()
{
	char hwname[256];
	auto type = static_cast<FilterType>(m_parameters[m_filterTypeName].GetIntVal());
	switch(type)
	{
		case FILTER_TYPE_LOWPASS:
			snprintf(hwname, sizeof(hwname), "LPF(%s, %s)",
				GetInputDisplayName(0).c_str(),
				m_parameters[m_freqHighName].ToString().c_str());
			break;

		case FILTER_TYPE_HIGHPASS:
			snprintf(hwname, sizeof(hwname), "HPF(%s, %s)",
				GetInputDisplayName(0).c_str(),
				m_parameters[m_freqLowName].ToString().c_str());
			break;

		case FILTER_TYPE_BANDPASS:
			snprintf(hwname, sizeof(hwname), "BPF(%s, %s, %s)",
				GetInputDisplayName(0).c_str(),
				m_parameters[m_freqLowName].ToString().c_str(),
				m_parameters[m_freqHighName].ToString().c_str());
			break;

		case FILTER_TYPE_NOTCH:
			snprintf(hwname, sizeof(hwname), "Notch(%s, %s, %s)",
				GetInputDisplayName(0).c_str(),
				m_parameters[m_freqLowName].ToString().c_str(),
				m_parameters[m_freqHighName].ToString().c_str());
			break;

	}
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string FIRFilter::GetProtocolName()
{
	return "FIR Filter";
}

bool FIRFilter::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool FIRFilter::NeedsConfig()
{
	return true;
}

double FIRFilter::GetVoltageRange()
{
	return m_range;
}

double FIRFilter::GetOffset()
{
	return m_offset;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void FIRFilter::Refresh()
{
	//Sanity check
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	//Get input data
	auto din = GetAnalogInputWaveform(0);

	//Assume the input is dense packed, get the sample frequency
	int64_t fs_per_sample = din->m_timescale;
	float sample_hz = FS_PER_SECOND / fs_per_sample;

	//Calculate limits for our filter
	float nyquist = sample_hz / 2;
	float flo = m_parameters[m_freqLowName].GetFloatVal();
	float fhi = m_parameters[m_freqHighName].GetFloatVal();
	auto type = static_cast<FilterType>(m_parameters[m_filterTypeName].GetIntVal());
	if(type == FILTER_TYPE_LOWPASS)
		flo = 0;
	else if(type == FILTER_TYPE_HIGHPASS)
		fhi = nyquist;
	else
	{
		//Swap high/low if they get swapped
		if(fhi < flo)
		{
			float ftmp = flo;
			flo = fhi;
			fhi = ftmp;
		}
	}
	flo = max(flo, 0.0f);
	fhi = min(fhi, nyquist);

	//Calculate filter order
	size_t filterlen = m_parameters[m_filterLengthName].GetIntVal();
	float atten = m_parameters[m_stopbandAttenName].GetFloatVal();
	if(filterlen == 0)
		filterlen = (atten / 22) * (sample_hz / (fhi - flo) );
	filterlen |= 1;	//force length to be odd

	//Create the filter coefficients (TODO: cache this)
	vector<float> coeffs;
	coeffs.resize(filterlen);
	CalculateFilterCoefficients(
		coeffs,
		flo / nyquist,
		fhi / nyquist,
		atten,
		type
		);

	//Set up output
	m_xAxisUnit = m_inputs[0].m_channel->GetXAxisUnits();
	m_yAxisUnit = m_inputs[0].m_channel->GetYAxisUnits();
	size_t radius = (filterlen - 1) / 2;
	auto cap = SetupOutputWaveform(din, 0, 0, filterlen);

	//Run the actual filter
	float vmin;
	float vmax;
	DoFilterKernel(coeffs, din, cap, vmin, vmax);

	//Shift output to compensate for filter group delay
	cap->m_triggerPhase = (radius * fs_per_sample) + din->m_triggerPhase;

	//Calculate bounds
	m_max = max(m_max, vmax);
	m_min = min(m_min, vmin);
	m_range = (m_max - m_min) * 1.05;
	m_offset = -( (m_max - m_min)/2 + m_min );
}

void FIRFilter::DoFilterKernel(
	vector<float>& coefficients,
	AnalogWaveform* din,
	AnalogWaveform* cap,
	float& vmin,
	float& vmax)
{
	if(g_hasAvx512F)
		DoFilterKernelAVX512F(coefficients, din, cap, vmin, vmax);
	else if(g_hasAvx2)
		DoFilterKernelAVX2(coefficients, din, cap, vmin, vmax);
	else
		DoFilterKernelGeneric(coefficients, din, cap, vmin, vmax);
}

/**
	@brief Performs a FIR filter (does not assume symmetric)
 */
void FIRFilter::DoFilterKernelGeneric(
	vector<float>& coefficients,
	AnalogWaveform* din,
	AnalogWaveform* cap,
	float& vmin,
	float& vmax)
{
	//Setup
	vmin = FLT_MAX;
	vmax = -FLT_MAX;
	size_t len = din->m_samples.size();
	size_t filterlen = coefficients.size();
	size_t end = len - filterlen;

	//Do the filter
	for(size_t i=0; i<end; i++)
	{
		float v = 0;
		for(size_t j=0; j<filterlen; j++)
			v += din->m_samples[i + j] * coefficients[j];

		vmin = min(vmin, v);
		vmax = max(vmax, v);

		cap->m_samples[i]	= v;
	}
}

/**
	@brief Optimized FIR implementation

	Uses AVX2, but not AVX512 or FMA.
 */
__attribute__((target("avx2")))
void FIRFilter::DoFilterKernelAVX2(
	vector<float>& coefficients,
	AnalogWaveform* din,
	AnalogWaveform* cap,
	float& vmin,
	float& vmax)
{
	__m256 vmin_x8 = { FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX };
	__m256 vmax_x8 = { -FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX };

	//Save some pointers and sizes
	size_t len = din->m_samples.size();
	size_t filterlen = coefficients.size();
	size_t end = len - filterlen;
	size_t end_rounded = end - (end % 64);
	float* pin = (float*)&din->m_samples[0];
	float* pout = (float*)&cap->m_samples[0];

	//Vectorized and unrolled outer loop
	size_t i=0;
	for(; i<end_rounded; i += 64)
	{
		float* base = pin + i;

		//First tap
		__m256 coeff		= _mm256_set1_ps(coefficients[0]);

		__m256 vin_a		= _mm256_loadu_ps(base + 0);
		__m256 vin_b		= _mm256_loadu_ps(base + 8);
		__m256 vin_c		= _mm256_loadu_ps(base + 16);
		__m256 vin_d		= _mm256_loadu_ps(base + 24);
		__m256 vin_e		= _mm256_loadu_ps(base + 32);
		__m256 vin_f		= _mm256_loadu_ps(base + 40);
		__m256 vin_g		= _mm256_loadu_ps(base + 48);
		__m256 vin_h		= _mm256_loadu_ps(base + 56);

		__m256 v_a			= _mm256_mul_ps(coeff, vin_a);
		__m256 v_b			= _mm256_mul_ps(coeff, vin_b);
		__m256 v_c			= _mm256_mul_ps(coeff, vin_c);
		__m256 v_d			= _mm256_mul_ps(coeff, vin_d);
		__m256 v_e			= _mm256_mul_ps(coeff, vin_e);
		__m256 v_f			= _mm256_mul_ps(coeff, vin_f);
		__m256 v_g			= _mm256_mul_ps(coeff, vin_g);
		__m256 v_h			= _mm256_mul_ps(coeff, vin_h);

		//Subsequent taps
		for(size_t j=1; j<filterlen; j++)
		{
			coeff			= _mm256_set1_ps(coefficients[j]);

			vin_a			= _mm256_loadu_ps(base + j + 0);
			vin_b			= _mm256_loadu_ps(base + j + 8);
			vin_c			= _mm256_loadu_ps(base + j + 16);
			vin_d			= _mm256_loadu_ps(base + j + 24);
			vin_e			= _mm256_loadu_ps(base + j + 32);
			vin_f			= _mm256_loadu_ps(base + j + 40);
			vin_g			= _mm256_loadu_ps(base + j + 48);
			vin_h			= _mm256_loadu_ps(base + j + 56);

			__m256 prod_a	= _mm256_mul_ps(coeff, vin_a);
			__m256 prod_b	= _mm256_mul_ps(coeff, vin_b);
			__m256 prod_c	= _mm256_mul_ps(coeff, vin_c);
			__m256 prod_d	= _mm256_mul_ps(coeff, vin_d);
			__m256 prod_e	= _mm256_mul_ps(coeff, vin_e);
			__m256 prod_f	= _mm256_mul_ps(coeff, vin_f);
			__m256 prod_g	= _mm256_mul_ps(coeff, vin_g);
			__m256 prod_h	= _mm256_mul_ps(coeff, vin_h);

			v_a				= _mm256_add_ps(prod_a, v_a);
			v_b				= _mm256_add_ps(prod_b, v_b);
			v_c				= _mm256_add_ps(prod_c, v_c);
			v_d				= _mm256_add_ps(prod_d, v_d);
			v_e				= _mm256_add_ps(prod_e, v_e);
			v_f				= _mm256_add_ps(prod_f, v_f);
			v_g				= _mm256_add_ps(prod_g, v_g);
			v_h				= _mm256_add_ps(prod_h, v_h);
		}

		//Store the output
		_mm256_store_ps(pout + i + 0,  v_a);
		_mm256_store_ps(pout + i + 8,  v_b);
		_mm256_store_ps(pout + i + 16, v_c);
		_mm256_store_ps(pout + i + 24, v_d);
		_mm256_store_ps(pout + i + 32, v_e);
		_mm256_store_ps(pout + i + 40, v_f);
		_mm256_store_ps(pout + i + 48, v_g);
		_mm256_store_ps(pout + i + 56, v_h);

		//Calculate min/max: First level
		__m256 min_ab	= _mm256_min_ps(v_a, v_b);
		__m256 min_cd	= _mm256_min_ps(v_c, v_d);
		__m256 min_ef	= _mm256_min_ps(v_e, v_f);
		__m256 min_gh	= _mm256_min_ps(v_g, v_h);

		__m256 max_ab	= _mm256_max_ps(v_a, v_b);
		__m256 max_cd	= _mm256_max_ps(v_c, v_d);
		__m256 max_ef	= _mm256_max_ps(v_e, v_f);
		__m256 max_gh	= _mm256_max_ps(v_g, v_h);

		//Min/max: second level
		__m256 min_abcd	= _mm256_min_ps(min_ab, min_cd);
		__m256 min_efgh	= _mm256_min_ps(min_ef, min_gh);
		__m256 max_abcd	= _mm256_max_ps(max_ab, max_cd);
		__m256 max_efgh	= _mm256_max_ps(max_ef, max_gh);

		//Min/max: third level
		__m256 min_l3	= _mm256_min_ps(min_abcd, min_efgh);
		__m256 max_l3	= _mm256_max_ps(max_abcd, max_efgh);

		//Min/max: final reduction
		vmin_x8 = _mm256_min_ps(vmin_x8, min_l3);
		vmax_x8 = _mm256_max_ps(vmax_x8, max_l3);
	}

	//Horizontal reduction of vector min/max
	float tmp_min[8] __attribute__((aligned(32)));
	float tmp_max[8] __attribute__((aligned(32)));
	_mm256_store_ps(tmp_min, vmin_x8);
	_mm256_store_ps(tmp_max, vmax_x8);
	for(int j=0; j<8; j++)
	{
		vmin = min(vmin, tmp_min[j]);
		vmax = max(vmax, tmp_max[j]);
	}

	//Catch any stragglers
	for(; i<end_rounded; i++)
	{
		float v = 0;
		for(size_t j=0; j<filterlen; j++)
			v += din->m_samples[i + j] * coefficients[j];

		vmin = min(vmin, v);
		vmax = max(vmax, v);

		cap->m_samples[i]	= v;
	}
}

/**
	@brief Optimized AVX512F implementation
 */
__attribute__((target("avx512f")))
void FIRFilter::DoFilterKernelAVX512F(
	vector<float>& coefficients,
	AnalogWaveform* din,
	AnalogWaveform* cap,
	float& vmin,
	float& vmax)
{
	__m512 vmin_x16 = _mm512_set1_ps(FLT_MAX);
	__m512 vmax_x16 = _mm512_set1_ps(-FLT_MAX);

	//Save some pointers and sizes
	size_t len = din->m_samples.size();
	size_t filterlen = coefficients.size();
	size_t end = len - filterlen;
	size_t end_rounded = end - (end % 64);
	float* pin = (float*)&din->m_samples[0];
	float* pout = (float*)&cap->m_samples[0];

	//Vectorized and unrolled outer loop
	size_t i=0;
	for(; i<end_rounded; i += 64)
	{
		float* base = pin + i;

		//First tap
		__m512 coeff		= _mm512_set1_ps(coefficients[0]);

		__m512 vin_a		= _mm512_loadu_ps(base + 0);
		__m512 vin_b		= _mm512_loadu_ps(base + 16);
		__m512 vin_c		= _mm512_loadu_ps(base + 32);
		__m512 vin_d		= _mm512_loadu_ps(base + 48);

		__m512 v_a			= _mm512_mul_ps(coeff, vin_a);
		__m512 v_b			= _mm512_mul_ps(coeff, vin_b);
		__m512 v_c			= _mm512_mul_ps(coeff, vin_c);
		__m512 v_d			= _mm512_mul_ps(coeff, vin_d);

		//Subsequent taps
		for(size_t j=1; j<filterlen; j++)
		{
			coeff			= _mm512_set1_ps(coefficients[j]);

			vin_a			= _mm512_loadu_ps(base + j + 0);
			vin_b			= _mm512_loadu_ps(base + j + 16);
			vin_c			= _mm512_loadu_ps(base + j + 32);
			vin_d			= _mm512_loadu_ps(base + j + 48);

			v_a				= _mm512_fmadd_ps(coeff, vin_a, v_a);
			v_b				= _mm512_fmadd_ps(coeff, vin_b, v_b);
			v_c				= _mm512_fmadd_ps(coeff, vin_c, v_c);
			v_d				= _mm512_fmadd_ps(coeff, vin_d, v_d);
		}

		//Store the output
		_mm512_store_ps(pout + i + 0,  v_a);
		_mm512_store_ps(pout + i + 16,  v_b);
		_mm512_store_ps(pout + i + 32, v_c);
		_mm512_store_ps(pout + i + 48, v_d);

		//Calculate min/max: First level
		__m512 min_ab	= _mm512_min_ps(v_a, v_b);
		__m512 min_cd	= _mm512_min_ps(v_c, v_d);

		__m512 max_ab	= _mm512_max_ps(v_a, v_b);
		__m512 max_cd	= _mm512_max_ps(v_c, v_d);

		//Min/max: second level
		__m512 min_abcd	= _mm512_min_ps(min_ab, min_cd);
		__m512 max_abcd	= _mm512_max_ps(max_ab, max_cd);

		//Min/max: final reduction
		vmin_x16 = _mm512_min_ps(vmin_x16, min_abcd);
		vmax_x16 = _mm512_max_ps(vmax_x16, max_abcd);
	}

	//Horizontal reduction of vector min/max
	float tmp_min[16] __attribute__((aligned(64)));
	float tmp_max[16] __attribute__((aligned(64)));
	_mm512_store_ps(tmp_min, vmin_x16);
	_mm512_store_ps(tmp_max, vmax_x16);
	for(int j=0; j<16; j++)
	{
		vmin = min(vmin, tmp_min[j]);
		vmax = max(vmax, tmp_max[j]);
	}

	//Catch any stragglers
	for(; i<end_rounded; i++)
	{
		float v = 0;
		for(size_t j=0; j<filterlen; j++)
			v += din->m_samples[i + j] * coefficients[j];

		vmin = min(vmin, v);
		vmax = max(vmax, v);

		cap->m_samples[i]	= v;
	}
}

/**
	@brief Calculates FIR coefficients

	Based on public domain code at https://www.arc.id.au/FilterDesign.html

	Cutoff frequencies are specified in fractions of the Nyquist limit (Fsample/2).

	@param coefficients		Output buffer
	@param fa				Left side passband (0 for LPF)
	@param fb				Right side passband (1 for HPF)
	@param stopbandAtten	Stop-band attenuation, in dB
	@param type				Type of filter
 */
void FIRFilter::CalculateFilterCoefficients(
	vector<float>& coefficients,
	float fa,
	float fb,
	float stopbandAtten,
	FilterType type)
{
	//Calculate the impulse response of the filter
	size_t len = coefficients.size();
	size_t np = (len - 1) / 2;
	vector<float> impulse;
	impulse.push_back(fb-fa);
	for(size_t j=1; j<=np; j++)
		impulse.push_back( (sin(j*M_PI*fb) - sin(j*M_PI*fa)) /(j*M_PI) );

	//Calculate window scaling factor for stopband attenuation
	float alpha = 0;
	if(stopbandAtten < 21)
		alpha = 0;
	else if(stopbandAtten > 50)
		alpha = 0.1102 * (stopbandAtten - 8.7);
	else
		alpha = 0.5842 * pow(stopbandAtten-21, 0.4) + 0.07886*(stopbandAtten-21);

	//Final windowing (Kaiser-Bessel)
	float ia = Bessel(alpha);
	if(type == FILTER_TYPE_NOTCH)
	{
		for(size_t j=0; j<=np; j++)
			coefficients[np+j] = -impulse[j] * Bessel(alpha * sqrt(1 - ((j*j*1.0)/(np*np)))) / ia;
		coefficients[np] += 1;
	}
	else
	{
		for(size_t j=0; j<=np; j++)
			coefficients[np+j] = impulse[j] * Bessel(alpha * sqrt(1 - ((j*j*1.0)/(np*np)))) / ia;
	}
	for(size_t j=0; j<=np; j++)
		coefficients[j] = coefficients[len-1-j];
}

/**
	@brief 0th order Bessel function
 */
float FIRFilter::Bessel(float x)
{
	float d = 0;
	float ds = 1;
	float s = 1;
	while(ds > s*1e-6)
	{
		d += 2;
		ds *= (x*x)/(d*d);
		s += ds;
	}
	return s;
}
