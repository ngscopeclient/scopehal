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

#include "../scopehal/scopehal.h"
#include "../scopehal/AlignedAllocator.h"
#include "FFTFilter.h"
#include <immintrin.h>
#include "../scopehal/avx_mathfun.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

FFTFilter::FFTFilter(const string& color)
	: PeakDetectionFilter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_RF)
	, m_windowName("Window")
{
	m_xAxisUnit = Unit(Unit::UNIT_HZ);
	m_yAxisUnit = Unit(Unit::UNIT_DBM);

	//Set up channels
	CreateInput("din");

	m_cachedNumPoints = 0;
	m_cachedNumPointsFFT = 0;
	m_rdin = NULL;
	m_rdout = NULL;
	m_plan = NULL;

	//Default config
	m_range = 70;
	m_offset = 35;

	m_parameters[m_windowName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_windowName].AddEnumValue("Blackman-Harris", WINDOW_BLACKMAN_HARRIS);
	m_parameters[m_windowName].AddEnumValue("Hamming", WINDOW_HAMMING);
	m_parameters[m_windowName].AddEnumValue("Hann", WINDOW_HANN);
	m_parameters[m_windowName].AddEnumValue("Rectangular", WINDOW_RECTANGULAR);
	m_parameters[m_windowName].SetIntVal(WINDOW_HAMMING);
}

FFTFilter::~FFTFilter()
{
	if(m_rdin)
		g_floatVectorAllocator.deallocate(m_rdin);
	if(m_rdout)
		g_floatVectorAllocator.deallocate(m_rdout);
	if(m_plan)
		ffts_free(m_plan);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool FFTFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

double FFTFilter::GetOffset()
{
	return m_offset;
}

double FFTFilter::GetVoltageRange()
{
	return m_range;
}

void FFTFilter::SetVoltageRange(double range)
{
	m_range = range;
}

void FFTFilter::SetOffset(double offset)
{
	m_offset = offset;
}

string FFTFilter::GetProtocolName()
{
	return "FFT";
}

bool FFTFilter::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool FFTFilter::NeedsConfig()
{
	//we auto-select the midpoint as our threshold
	return false;
}

void FFTFilter::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "FFT(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void FFTFilter::ReallocateBuffers(size_t npoints_raw, size_t npoints, size_t nouts)
{
	m_cachedNumPoints = npoints_raw;

	if(m_cachedNumPointsFFT != npoints)
	{
		m_cachedNumPointsFFT = npoints;

		if(m_plan)
			ffts_free(m_plan);

		m_plan = ffts_init_1d_real(npoints, FFTS_FORWARD);
	}

	if(m_rdin)
		g_floatVectorAllocator.deallocate(m_rdin);
	if(m_rdout)
		g_floatVectorAllocator.deallocate(m_rdout);

	m_rdin = g_floatVectorAllocator.allocate(npoints);
	m_rdout = g_floatVectorAllocator.allocate(2*nouts);
}

void FFTFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}
	auto din = GetAnalogInputWaveform(0);

	//Round size up to next power of two
	const size_t npoints_raw = din->m_samples.size();
	const size_t npoints = next_pow2(npoints_raw);
	LogTrace("FFTFilter: processing %zu raw points\n", npoints_raw);
	LogTrace("Rounded to %zu\n", npoints);

	//Reallocate buffers if size has changed
	const size_t nouts = npoints/2 + 1;
	if(m_cachedNumPoints != npoints_raw)
		ReallocateBuffers(npoints_raw, npoints, nouts);
	LogTrace("Output: %zu\n", nouts);

	//Copy the input with windowing, then zero pad to the desired input length
	ApplyWindow(
		(float*)&din->m_samples[0],
		npoints_raw,
		m_rdin,
		static_cast<WindowFunction>(m_parameters[m_windowName].GetIntVal()));
	memset(m_rdin + npoints_raw, 0, (npoints - npoints_raw) * sizeof(float));

	double fs = din->m_timescale * (din->m_offsets[1] - din->m_offsets[0]);
	DoRefresh(din, fs, npoints, nouts, true);
}

void FFTFilter::DoRefresh(AnalogWaveform* din, double fs_per_sample, size_t npoints, size_t nouts, bool log_output)
{
	//Calculate the FFT
	ffts_execute(m_plan, m_rdin, m_rdout);

	//Set up output and copy timestamps
	auto cap = new AnalogWaveform;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;

	//Calculate size of each bin
	double sample_ghz = 1e6 / fs_per_sample;
	double bin_hz = round((0.5f * sample_ghz * 1e9f) / nouts);
	cap->m_timescale = bin_hz;
	LogTrace("bin_hz: %f\n", bin_hz);

	//Normalize magnitudes
	cap->Resize(nouts);
	if(log_output)
	{
		if(g_hasAvx2)
			NormalizeOutputLogAVX2(cap, nouts, npoints);
		else
			NormalizeOutputLog(cap, nouts, npoints);
	}
	else
	{
		if(g_hasAvx2)
			NormalizeOutputLinearAVX2(cap, nouts, npoints);
		else
			NormalizeOutputLinear(cap, nouts, npoints);
	}

	//Peak search
	FindPeaks(cap);

	//Done
	SetData(cap, 0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Normalization

/**
	@brief Normalize FFT output and convert to dBm (unoptimized C++ implementation)
 */
void FFTFilter::NormalizeOutputLog(AnalogWaveform* cap, size_t nouts, size_t npoints)
{
	//assume constant 50 ohms for now
	const float impedance = 50;
	float scale = 2.0 / npoints;
	for(size_t i=0; i<nouts; i++)
	{
		cap->m_offsets[i] = i;
		cap->m_durations[i] = 1;

		float real = m_rdout[i*2];
		float imag = m_rdout[i*2 + 1];

		float voltage = sqrtf(real*real + imag*imag) * scale;

		//Convert to dBm
		cap->m_samples[i] = (10 * log10(voltage*voltage / impedance) + 30);
	}
}

/**
	@brief Normalize FFT output and output in native Y-axis units (unoptimized C++ implementation)
 */
void FFTFilter::NormalizeOutputLinear(AnalogWaveform* cap, size_t nouts, size_t npoints)
{
	float scale = 2.0 / npoints;
	for(size_t i=0; i<nouts; i++)
	{
		cap->m_offsets[i] = i;
		cap->m_durations[i] = 1;

		float real = m_rdout[i*2];
		float imag = m_rdout[i*2 + 1];

		cap->m_samples[i] = sqrtf(real*real + imag*imag) * scale;
	}
}

/**
	@brief Normalize FFT output and convert to dBm (optimized AVX2 implementation)
 */
__attribute__((target("avx2")))
void FFTFilter::NormalizeOutputLogAVX2(AnalogWaveform* cap, size_t nouts, size_t npoints)
{
	int64_t* offs = (int64_t*)&cap->m_offsets[0];
	int64_t* durs = (int64_t*)&cap->m_durations[0];

	size_t end = nouts - (nouts % 8);

	int64_t __attribute__ ((aligned(32))) ones_x4[] = {1, 1, 1, 1};
	int64_t __attribute__ ((aligned(32))) fours_x4[] = {4, 4, 4, 4};
	int64_t __attribute__ ((aligned(32))) count_x4[] = {0, 1, 2, 3};

	__m256i all_ones = _mm256_load_si256(reinterpret_cast<__m256i*>(ones_x4));
	__m256i all_fours = _mm256_load_si256(reinterpret_cast<__m256i*>(fours_x4));
	__m256i counts = _mm256_load_si256(reinterpret_cast<__m256i*>(count_x4));

	//double since we only look at positive half
	float norm = 2.0f / npoints;
	__m256 norm_f = { norm, norm, norm, norm, norm, norm, norm, norm };

	//1 / nominal line impedance
	float impedance = 50;
	__m256 inv_imp =
	{
		1/impedance, 1/impedance, 1/impedance, 1/impedance,
		1/impedance, 1/impedance, 1/impedance, 1/impedance
	};

	//Constant values for dBm conversion
	__m256 const_10 = {10, 10, 10, 10, 10, 10, 10, 10 };
	__m256 const_30 = {30, 30, 30, 30, 30, 30, 30, 30 };

	float* pout = (float*)&cap->m_samples[0];

	//Vectorized processing (8 samples per iteration)
	for(size_t k=0; k<end; k += 8)
	{
		//Fill duration
		_mm256_store_si256(reinterpret_cast<__m256i*>(durs + k), all_ones);
		_mm256_store_si256(reinterpret_cast<__m256i*>(durs + k + 4), all_ones);

		//Fill offset
		_mm256_store_si256(reinterpret_cast<__m256i*>(offs + k), counts);
		counts = _mm256_add_epi64(counts, all_fours);
		_mm256_store_si256(reinterpret_cast<__m256i*>(offs + k + 4), counts);
		counts = _mm256_add_epi64(counts, all_fours);

		//Read interleaved real/imaginary FFT output (riririri riririri)
		__m256 din0 = _mm256_load_ps(m_rdout + k*2);
		__m256 din1 = _mm256_load_ps(m_rdout + k*2 + 8);

		//Step 1: Shuffle 32-bit values within 128-bit lanes to get rriirrii rriirrii.
		din0 = _mm256_permute_ps(din0, 0xd8);
		din1 = _mm256_permute_ps(din1, 0xd8);

		//Step 2: Shuffle 64-bit values to get rrrriiii rrrriiii.
		__m256i block0 = _mm256_permute4x64_epi64(_mm256_castps_si256(din0), 0xd8);
		__m256i block1 = _mm256_permute4x64_epi64(_mm256_castps_si256(din1), 0xd8);

		//Step 3: Shuffle 128-bit values to get rrrrrrrr iiiiiiii.
		__m256 real = _mm256_castsi256_ps(_mm256_permute2x128_si256(block0, block1, 0x20));
		__m256 imag = _mm256_castsi256_ps(_mm256_permute2x128_si256(block0, block1, 0x31));

		//Actual vector normalization
		real = _mm256_mul_ps(real, real);
		imag = _mm256_mul_ps(imag, imag);
		__m256 sum = _mm256_add_ps(real, imag);
		__m256 mag = _mm256_sqrt_ps(sum);
		mag = _mm256_mul_ps(mag, norm_f);

		//Convert to watts
		__m256 vsq = _mm256_mul_ps(mag, mag);
		__m256 watts = _mm256_mul_ps(vsq, inv_imp);

		//TODO: figure out better way to do efficient logarithm
		_mm256_store_ps(pout + k, watts);
		for(size_t i=k; i<k+8; i++)
			pout[i] = log10(pout[i]);
		__m256 logpwr = _mm256_load_ps(pout + k);

		//Final scaling
		logpwr = _mm256_mul_ps(logpwr, const_10);
		logpwr = _mm256_add_ps(logpwr, const_30);

		//and store the actual
		_mm256_store_ps(pout + k, logpwr);
	}

	//Get any extras we didn't get in the SIMD loop
	for(size_t k=end; k<nouts; k++)
	{
		cap->m_offsets[k] = k;
		cap->m_durations[k] = 1;

		float real = m_rdout[k*2];
		float imag = m_rdout[k*2 + 1];

		float voltage = sqrtf(real*real + imag*imag) / npoints;

		//Convert to dBm
		pout[k] = (10 * log10(voltage*voltage / impedance) + 30);
	}
}

/**
	@brief Normalize FFT output and keep in native units (optimized AVX2 implementation)
 */
__attribute__((target("avx2")))
void FFTFilter::NormalizeOutputLinearAVX2(AnalogWaveform* cap, size_t nouts, size_t npoints)
{
	int64_t* offs = (int64_t*)&cap->m_offsets[0];
	int64_t* durs = (int64_t*)&cap->m_durations[0];

	size_t end = nouts - (nouts % 8);

	int64_t __attribute__ ((aligned(32))) ones_x4[] = {1, 1, 1, 1};
	int64_t __attribute__ ((aligned(32))) fours_x4[] = {4, 4, 4, 4};
	int64_t __attribute__ ((aligned(32))) count_x4[] = {0, 1, 2, 3};

	__m256i all_ones = _mm256_load_si256(reinterpret_cast<__m256i*>(ones_x4));
	__m256i all_fours = _mm256_load_si256(reinterpret_cast<__m256i*>(fours_x4));
	__m256i counts = _mm256_load_si256(reinterpret_cast<__m256i*>(count_x4));

	//double since we only look at positive half
	float norm = 2.0f / npoints;
	__m256 norm_f = { norm, norm, norm, norm, norm, norm, norm, norm };

	float* pout = (float*)&cap->m_samples[0];

	//Vectorized processing (8 samples per iteration)
	for(size_t k=0; k<end; k += 8)
	{
		//Fill duration
		_mm256_store_si256(reinterpret_cast<__m256i*>(durs + k), all_ones);
		_mm256_store_si256(reinterpret_cast<__m256i*>(durs + k + 4), all_ones);

		//Fill offset
		_mm256_store_si256(reinterpret_cast<__m256i*>(offs + k), counts);
		counts = _mm256_add_epi64(counts, all_fours);
		_mm256_store_si256(reinterpret_cast<__m256i*>(offs + k + 4), counts);
		counts = _mm256_add_epi64(counts, all_fours);

		//Read interleaved real/imaginary FFT output (riririri riririri)
		__m256 din0 = _mm256_load_ps(m_rdout + k*2);
		__m256 din1 = _mm256_load_ps(m_rdout + k*2 + 8);

		//Step 1: Shuffle 32-bit values within 128-bit lanes to get rriirrii rriirrii.
		din0 = _mm256_permute_ps(din0, 0xd8);
		din1 = _mm256_permute_ps(din1, 0xd8);

		//Step 2: Shuffle 64-bit values to get rrrriiii rrrriiii.
		__m256i block0 = _mm256_permute4x64_epi64(_mm256_castps_si256(din0), 0xd8);
		__m256i block1 = _mm256_permute4x64_epi64(_mm256_castps_si256(din1), 0xd8);

		//Step 3: Shuffle 128-bit values to get rrrrrrrr iiiiiiii.
		__m256 real = _mm256_castsi256_ps(_mm256_permute2x128_si256(block0, block1, 0x20));
		__m256 imag = _mm256_castsi256_ps(_mm256_permute2x128_si256(block0, block1, 0x31));

		//Actual vector normalization
		real = _mm256_mul_ps(real, real);
		imag = _mm256_mul_ps(imag, imag);
		__m256 sum = _mm256_add_ps(real, imag);
		__m256 mag = _mm256_sqrt_ps(sum);
		mag = _mm256_mul_ps(mag, norm_f);

		//and store the result
		_mm256_store_ps(pout + k, mag);
	}

	//Get any extras we didn't get in the SIMD loop
	for(size_t k=end; k<nouts; k++)
	{
		cap->m_offsets[k] = k;
		cap->m_durations[k] = 1;

		float real = m_rdout[k*2];
		float imag = m_rdout[k*2 + 1];

		pout[k] = sqrtf(real*real + imag*imag) / npoints;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Window functions

void FFTFilter::ApplyWindow(const float* data, size_t len, float* out, WindowFunction func)
{
	switch(func)
	{
		case WINDOW_BLACKMAN_HARRIS:
			if(g_hasAvx2)
				return BlackmanHarrisWindowAVX2(data, len, out);
			else
				return BlackmanHarrisWindow(data, len, out);

		case WINDOW_HANN:
			return HannWindow(data, len, out);

		case WINDOW_HAMMING:
			return HammingWindow(data, len, out);

		case WINDOW_RECTANGULAR:
		default:
			memcpy(out, data, len * sizeof(float));
	}
}

//TODO: vectorization
void FFTFilter::CosineSumWindow(const float* data, size_t len, float* out, float alpha0)
{
	float alpha1 = 1 - alpha0;
	float scale = 2.0f * (float)M_PI / len;

	float* aligned_data = (float*)__builtin_assume_aligned(data, 32);
	float* aligned_out = (float*)__builtin_assume_aligned(out, 32);
	for(size_t i=0; i<len; i++)
	{
		float w = alpha0 - alpha1*cosf(i*scale);
		aligned_out[i] = w * aligned_data[i];
	}
}

__attribute__((target("avx2")))
void FFTFilter::CosineSumWindowAVX2(const float* data, size_t len, float* out, float alpha0)
{
	float alpha1 = 1 - alpha0;
	float scale = 2.0f * (float)M_PI / len;

	float* aligned_data = (float*)__builtin_assume_aligned(data, 32);
	float* aligned_out = (float*)__builtin_assume_aligned(out, 32);

	__m256 count_x8		= { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f };
	__m256 eights_x8	= { 8.0f, 8.0f, 8.0f, 8.0f, 8.0f, 8.0f, 8.0f, 8.0f };
	__m256 scale_x8		= { scale, scale, scale, scale, scale, scale, scale, scale };
	__m256 alpha0_x8	= { alpha0, alpha0, alpha0, alpha0, alpha0, alpha0, alpha0, alpha0 };
	__m256 alpha1_x8	= { alpha1, alpha1, alpha1, alpha1, alpha1, alpha1, alpha1, alpha1 };

	size_t i;
	size_t len_rounded = len - (len % 8);
	for(i=0; i<len_rounded; i += 8)
	{
		__m256 vscale	= _mm256_mul_ps(count_x8, scale_x8);
		__m256 vcos		= _mm256_cos_ps(vscale);
		__m256 rhs 		= _mm256_mul_ps(vcos, alpha1_x8);
		__m256 w		= _mm256_sub_ps(alpha0_x8, rhs);

		__m256 din		= _mm256_load_ps(aligned_data + i);
		__m256 dout		= _mm256_mul_ps(din, w);
		_mm256_store_ps(aligned_out + i, dout);

		count_x8 = _mm256_add_ps(count_x8, eights_x8);
	}

	//Last few iterations
	for(; i<len; i++)
	{
		float w = alpha0 - alpha1*cosf(i*scale);
		out[i] = w * data[i];
	}
}

void FFTFilter::BlackmanHarrisWindow(const float* data, size_t len, float* out)
{
	float alpha0 = 0.35875;
	float alpha1 = 0.48829;
	float alpha2 = 0.14128;
	float alpha3 = 0.01168;
	float scale = 2 *(float)M_PI / len;

	for(size_t i=0; i<len; i++)
	{
		float num = i * scale;
		float w =
			alpha0 -
			alpha1 * cosf(num) +
			alpha2 * cosf(2*num) -
			alpha3 * cosf(6*num);
		out[i] = w * data[i];
	}
}

__attribute__((target("avx2")))
void FFTFilter::BlackmanHarrisWindowAVX2(const float* data, size_t len, float* out)
{
	float alpha0 = 0.35875;
	float alpha1 = 0.48829;
	float alpha2 = 0.14128;
	float alpha3 = 0.01168;
	float scale = 2 *(float)M_PI / len;

	float* aligned_data = (float*)__builtin_assume_aligned(data, 32);
	float* aligned_out = (float*)__builtin_assume_aligned(out, 32);

	__m256 count_x8		= { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f };
	__m256 eights_x8	= { 8.0f, 8.0f, 8.0f, 8.0f, 8.0f, 8.0f, 8.0f, 8.0f };
	__m256 scale_x8		= { scale, scale, scale, scale, scale, scale, scale, scale };
	__m256 alpha0_x8	= { alpha0, alpha0, alpha0, alpha0, alpha0, alpha0, alpha0, alpha0 };
	__m256 alpha1_x8	= { alpha1, alpha1, alpha1, alpha1, alpha1, alpha1, alpha1, alpha1 };
	__m256 alpha2_x8	= { alpha2, alpha2, alpha2, alpha2, alpha2, alpha2, alpha2, alpha2 };
	__m256 alpha3_x8	= { alpha3, alpha3, alpha3, alpha3, alpha3, alpha3, alpha3, alpha3 };
	__m256 two_x8		= { 2, 2, 2, 2, 2, 2, 2, 2 };

	size_t i;
	size_t len_rounded = len - (len % 8);
	for(i=0; i<len_rounded; i += 8)
	{
		__m256 vscale		= _mm256_mul_ps(count_x8, scale_x8);
		__m256 vscale_x2	= _mm256_mul_ps(vscale, two_x8);
		__m256 vscale_x3	= _mm256_add_ps(vscale, vscale_x2);

		__m256 term1		= _mm256_cos_ps(vscale);
		__m256 term2		= _mm256_cos_ps(vscale_x2);
		__m256 term3		= _mm256_cos_ps(vscale_x3);
		term1 				= _mm256_mul_ps(term1, alpha1_x8);
		term2 				= _mm256_mul_ps(term2, alpha2_x8);
		term3 				= _mm256_mul_ps(term3, alpha3_x8);
		__m256 w			= _mm256_sub_ps(alpha0_x8, term1);
		w					= _mm256_add_ps(w, term2);
		w					= _mm256_add_ps(w, term3);

		__m256 din			= _mm256_load_ps(aligned_data + i);
		__m256 dout			= _mm256_mul_ps(din, w);
		_mm256_store_ps(aligned_out + i, dout);

		count_x8 = _mm256_add_ps(count_x8, eights_x8);
	}

	for(; i<len; i++)
	{
		float num = i * scale;
		float w =
			alpha0 -
			alpha1 * cosf(num) +
			alpha2 * cosf(2*num) -
			alpha3 * cosf(3*num);
		out[i] = w * data[i];
	}
}

void FFTFilter::HannWindow(const float* data, size_t len, float* out)
{
	if(g_hasAvx2)
		CosineSumWindowAVX2(data, len, out, 0.5);
	else
		CosineSumWindow(data, len, out, 0.5);
}

void FFTFilter::HammingWindow(const float* data, size_t len, float* out)
{
	if(g_hasAvx2)
		CosineSumWindowAVX2(data, len, out, 25.0f / 46);
	else
		CosineSumWindow(data, len, out, 25.0f / 46);
}
