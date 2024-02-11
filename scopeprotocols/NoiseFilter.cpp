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

#include "../scopehal/scopehal.h"
#include "NoiseFilter.h"
#ifdef __x86_64__
#include <immintrin.h>
#include "avx_mathfun.h"
#endif

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

NoiseFilter::NoiseFilter(const string& color)
	: Filter(color, CAT_GENERATION)
	, m_stdevname("Deviation")
	, m_twister(rand())
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput("din");

	m_parameters[m_stdevname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_stdevname].SetFloatVal(0.005);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool NoiseFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string NoiseFilter::GetProtocolName()
{
	return "Noise";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void NoiseFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndUniformAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	auto din = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(0));
	din->PrepareForCpuAccess();
	size_t len = din->size();

	float stdev = m_parameters[m_stdevname].GetFloatVal();
	auto cap = SetupEmptyUniformAnalogOutputWaveform(din, 0);
	cap->Resize(len);
	cap->PrepareForCpuAccess();

	#ifdef __x86_64__
	if(g_hasAvx2)
		CopyWithAwgnAVX2((float*)&cap->m_samples[0], (float*)&din->m_samples[0], len, stdev);
	else
	#endif
		CopyWithAwgnNative((float*)&cap->m_samples[0], (float*)&din->m_samples[0], len, stdev);

	cap->MarkModifiedFromCpu();
}

void NoiseFilter::CopyWithAwgnNative(float* dest, float* src, size_t len, float sigma)
{
	//Add the noise
	//gcc 8.x / 9.x have false positive here (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=99536)
	minstd_rand rng(m_twister());
	normal_distribution<> noise(0, sigma);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
	for(size_t i=0; i<len; i++)
		dest[i] = src[i] + noise(rng);
#pragma GCC diagnostic pop
}

#ifdef __x86_64__
__attribute__((target("avx2")))
void NoiseFilter::CopyWithAwgnAVX2(float* dest, float* src, size_t len, float sigma)
{
	size_t end = len - (len % 16);

	//Box-Muller setup
	__m256 vsigma		= _mm256_set1_ps(sigma);
	__m256 vmtwo		= _mm256_set1_ps(-2.0f);
	__m256 vtpi			= _mm256_set1_ps(M_PI * 2);
	__m256 norm1;
	__m256 norm2;

	//Random number generator (xorshift32 LFSR)
	__m256i rng_state1 = _mm256_set_epi32(
		m_twister(), m_twister(), m_twister(), m_twister(), m_twister(), m_twister(), m_twister(), m_twister());
	__m256i rng_state2 = _mm256_set_epi32(
		m_twister(), m_twister(), m_twister(), m_twister(), m_twister(), m_twister(), m_twister(), m_twister());
	__m256 rng_scale	= _mm256_set1_ps(1.0f / static_cast<float>(0xffffffff));

	//Create normally distributed output using Box-Muller
	for(size_t i=0; i<end; i += 16)
	{
		//Load input samples
		__m256 samples1		= _mm256_load_ps(src + i);
		__m256 samples2		= _mm256_load_ps(src + i + 8);

		//Generate two sets of eight random int32 values
		__m256i tmp1		= _mm256_slli_epi32(rng_state1, 13);
		__m256i tmp2		= _mm256_slli_epi32(rng_state2, 13);
		rng_state1			= _mm256_xor_si256(rng_state1, tmp1);
		rng_state2			= _mm256_xor_si256(rng_state2, tmp2);
		tmp1				= _mm256_srli_epi32(rng_state1, 17);
		tmp2				= _mm256_srli_epi32(rng_state2, 17);
		rng_state1			= _mm256_xor_si256(rng_state1, tmp1);
		rng_state2			= _mm256_xor_si256(rng_state2, tmp2);
		tmp1				= _mm256_slli_epi32(rng_state1, 5);
		tmp2				= _mm256_slli_epi32(rng_state2, 5);
		rng_state1			= _mm256_xor_si256(rng_state1, tmp1);
		rng_state2			= _mm256_xor_si256(rng_state2, tmp2);

		//Convert the random values to floating point in the range (0, 1)
		tmp1				= _mm256_abs_epi32(rng_state1);
		tmp2				= _mm256_abs_epi32(rng_state2);
		__m256 random1		= _mm256_cvtepi32_ps(tmp1);
		__m256 random2		= _mm256_cvtepi32_ps(tmp2);
		random1				= _mm256_mul_ps(random1, rng_scale);
		random2				= _mm256_mul_ps(random2, rng_scale);

		//Apply Box-Muller transformation
		__m256 mag			= _mm256_log_ps(random1);
		mag					= _mm256_mul_ps(mag, vmtwo);
		mag					= _mm256_sqrt_ps(mag);
		mag					= _mm256_mul_ps(mag, vsigma);
		__m256 rtpi			= _mm256_mul_ps(random2, vtpi);
		_mm256_sincos_ps(rtpi, &norm1, &norm2);
		norm1				= _mm256_mul_ps(mag, norm1);
		norm2				= _mm256_mul_ps(mag, norm2);

		//Add the noise
		__m256 out1			= _mm256_add_ps(samples1, norm1);
		__m256 out2			= _mm256_add_ps(samples2, norm2);

		//Write output
		_mm256_store_ps(dest + i, out1);
		_mm256_store_ps(dest + i + 8, out2);
	}

	//Process the last few samples
	//gcc 8.x / 9.x have false positive here (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=99536)
	minstd_rand rng(m_twister());
	normal_distribution<> noise(0, sigma);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
	for(size_t i=end; i<len; i++)
		dest[i] = src[i] + noise(rng);
#pragma GCC diagnostic pop
}
#endif /* __x86_64__ */
