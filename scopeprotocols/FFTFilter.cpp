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
#include "../scopehal/AlignedAllocator.h"
#include "FFTFilter.h"
#ifdef __x86_64__
#include <immintrin.h>
#include "../scopehal/avx_mathfun.h"
#endif

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

FFTFilter::FFTFilter(const string& color)
	: PeakDetectionFilter(color, CAT_RF)
	, m_windowName("Window")
	, m_roundingName("Length Rounding")
	, m_blackmanHarrisComputePipeline("shaders/BlackmanHarrisWindow.spv", 2, sizeof(WindowFunctionArgs))
	, m_rectangularComputePipeline("shaders/RectangularWindow.spv", 2, sizeof(WindowFunctionArgs))
	, m_cosineSumComputePipeline("shaders/CosineSumWindow.spv", 2, sizeof(WindowFunctionArgs))
	, m_complexToMagnitudeComputePipeline("shaders/ComplexToMagnitude.spv", 2, sizeof(ComplexToMagnitudeArgs))
	, m_complexToLogMagnitudeComputePipeline("shaders/ComplexToLogMagnitude.spv", 2, sizeof(ComplexToMagnitudeArgs))
{
	m_xAxisUnit = Unit(Unit::UNIT_HZ);
	AddStream(Unit(Unit::UNIT_DBM), "data", Stream::STREAM_TYPE_ANALOG);

	//Set up channels
	CreateInput("din");

	m_cachedNumPoints = 0;
	m_cachedNumPointsFFT = 0;
#ifndef _APPLE_SILICON
	m_plan = NULL;
#endif

	//Default config
	m_range = 70;
	m_offset = 35;

	m_parameters[m_windowName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_windowName].AddEnumValue("Blackman-Harris", WINDOW_BLACKMAN_HARRIS);
	m_parameters[m_windowName].AddEnumValue("Hamming", WINDOW_HAMMING);
	m_parameters[m_windowName].AddEnumValue("Hann", WINDOW_HANN);
	m_parameters[m_windowName].AddEnumValue("Rectangular", WINDOW_RECTANGULAR);
	m_parameters[m_windowName].SetIntVal(WINDOW_HAMMING);

	m_parameters[m_roundingName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_roundingName].AddEnumValue("Down (Truncate)", ROUND_TRUNCATE);
	m_parameters[m_roundingName].AddEnumValue("Up (Zero Pad)", ROUND_ZERO_PAD);
	m_parameters[m_roundingName].SetIntVal(ROUND_TRUNCATE);

	m_cachedGpuFilterEnabled = g_gpuFilterEnabled;
}

FFTFilter::~FFTFilter()
{
#ifndef _APPLE_SILICON
	if(m_plan)
		ffts_free(m_plan);
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool FFTFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

float FFTFilter::GetOffset(size_t /*stream*/)
{
	return m_offset;
}

float FFTFilter::GetVoltageRange(size_t /*stream*/)
{
	return m_range;
}

void FFTFilter::SetVoltageRange(float range, size_t /*stream*/)
{
	m_range = range;
}

void FFTFilter::SetOffset(float offset, size_t /*stream*/)
{
	m_offset = offset;
}

string FFTFilter::GetProtocolName()
{
	return "FFT";
}

Filter::DataLocation FFTFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void FFTFilter::ReallocateBuffers(size_t npoints_raw, size_t npoints, size_t nouts)
{
	m_cachedNumPoints = npoints_raw;

	if(m_cachedNumPointsFFT != npoints)
	{
		m_cachedNumPointsFFT = npoints;

	#ifndef _APPLE_SILICON
		if(g_gpuFilterEnabled)
			m_plan = nullptr;

		//CPU fallback
		else
		{
			if(m_plan)
				ffts_free(m_plan);
			m_plan = ffts_init_1d_real(npoints, FFTS_FORWARD);

			m_rdinbuf.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
			m_rdinbuf.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_NEVER);
			m_rdoutbuf.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
			m_rdoutbuf.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_NEVER);
		}
	#else
		if(!g_gpuFilterEnabled)
			LogFatal("FFTFilter cpu fallback not available on Apple Silicon (yet?)");
	#endif
	}

	//Update our FFT plan if it's out of date
	if(!g_gpuFilterEnabled)
		m_vkPlan = nullptr;
	else
	{
		m_rdinbuf.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_NEVER);
		m_rdinbuf.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
		m_rdoutbuf.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_NEVER);
		m_rdoutbuf.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);

		if(m_vkPlan)
		{
			if(m_vkPlan->size() != npoints)
				m_vkPlan = nullptr;
		}
		if(!m_vkPlan)
			m_vkPlan = make_unique<VulkanFFTPlan>(npoints, nouts, VulkanFFTPlan::DIRECTION_FORWARD);
	}

	m_rdinbuf.resize(npoints);
	m_rdoutbuf.resize(2*nouts);

	m_cachedGpuFilterEnabled = g_gpuFilterEnabled;
}

void FFTFilter::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndUniformAnalog())
	{
		SetData(NULL, 0);
		return;
	}
	auto din = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(0));

	const size_t npoints_raw = din->size();
	size_t npoints;
	if(m_parameters[m_roundingName].GetIntVal() == ROUND_TRUNCATE)
		npoints = prev_pow2(npoints_raw);
	else
		npoints = next_pow2(npoints_raw);
	LogTrace("FFTFilter: processing %zu raw points\n", npoints_raw);
	LogTrace("Rounded to %zu\n", npoints);

	//Reallocate buffers if size has changed
	const size_t nouts = npoints/2 + 1;
	if( (m_cachedNumPoints != npoints_raw) || (m_cachedGpuFilterEnabled != g_gpuFilterEnabled) )
		ReallocateBuffers(npoints_raw, npoints, nouts);
	LogTrace("Output: %zu\n", nouts);

	DoRefresh(din, din->m_samples, din->m_timescale, npoints, nouts, true, cmdBuf, queue);
}

void FFTFilter::DoRefresh(
	WaveformBase* din,
	AcceleratorBuffer<float>& data,
	double fs_per_sample,
	size_t npoints,
	size_t nouts,
	bool log_output,
	vk::raii::CommandBuffer& cmdBuf,
	shared_ptr<QueueHandle> queue)
{
	//Look up some parameters
	double sample_ghz = 1e6 / fs_per_sample;
	double bin_hz = round((0.5f * sample_ghz * 1e9f) / nouts);
	auto window = static_cast<WindowFunction>(m_parameters[m_windowName].GetIntVal());
	LogTrace("bin_hz: %f\n", bin_hz);

	//Set up output and copy time scales / configuration
	auto cap = SetupEmptyUniformAnalogOutputWaveform(din, 0);
	cap->m_triggerPhase = 1*bin_hz;
	cap->m_timescale = bin_hz;
	cap->Resize(nouts);

	//Output scale is based on the number of points we FFT that contain actual sample data.
	//(If we're zero padding, the zeroes don't contribute any power)
	size_t numActualSamples = min(data.size(), npoints);
	float scale = sqrt(2.0) / numActualSamples;

	//We also need to adjust the scale by the coherent power gain of the window function
	switch(window)
	{
		case WINDOW_HAMMING:
			scale *= 1.862;
			break;

		case WINDOW_HANN:
			scale *= 2.013;
			break;

		case WINDOW_BLACKMAN_HARRIS:
			scale *= 2.805;
			break;

		//unit
		case WINDOW_RECTANGULAR:
		default:
			break;
	}

	if(g_gpuFilterEnabled)
	{
		//Configure the window
		WindowFunctionArgs args;
		args.numActualSamples = numActualSamples;
		args.npoints = npoints;
		args.scale = 2 * M_PI / numActualSamples;
		switch(window)
		{
			case WINDOW_HANN:
				args.alpha0 = 0.5;
				break;

			case WINDOW_HAMMING:
				args.alpha0 = 25.0f / 46;
				break;

			default:
				args.alpha0 = 0;
				break;
		}
		args.alpha1 = 1 - args.alpha0;

		//Prepare to do all of our compute stuff in one dispatch call to reduce overhead
		cmdBuf.begin({});

		//Apply the window function
		ComputePipeline* wpipe = nullptr;
		switch(window)
		{
			case WINDOW_BLACKMAN_HARRIS:
				wpipe = &m_blackmanHarrisComputePipeline;
				break;

			case WINDOW_HANN:
			case WINDOW_HAMMING:
				wpipe = &m_cosineSumComputePipeline;
				break;

			default:
			case WINDOW_RECTANGULAR:
				wpipe = &m_rectangularComputePipeline;
				break;
		}
		wpipe->BindBufferNonblocking(0, data, cmdBuf);
		wpipe->BindBufferNonblocking(1, m_rdinbuf, cmdBuf, true);
		wpipe->Dispatch(cmdBuf, args, GetComputeBlockCount(npoints, 64));
		wpipe->AddComputeMemoryBarrier(cmdBuf);
		m_rdinbuf.MarkModifiedFromGpu();

		//Do the actual FFT operation
		m_vkPlan->AppendForward(m_rdinbuf, m_rdoutbuf, cmdBuf);

		//Convert complex to real
		ComputePipeline& pipe = log_output ?
			m_complexToLogMagnitudeComputePipeline : m_complexToMagnitudeComputePipeline;
		ComplexToMagnitudeArgs cargs;
		cargs.npoints = nouts;
		if(log_output)
		{
			const float impedance = 50;
			cargs.scale = scale * scale / impedance;
		}
		else
			cargs.scale = scale;
		pipe.BindBuffer(0, m_rdoutbuf);
		pipe.BindBuffer(1, cap->m_samples);
		pipe.AddComputeMemoryBarrier(cmdBuf);
		pipe.Dispatch(cmdBuf, cargs, GetComputeBlockCount(nouts, 64));

		//Done, block until the compute operations finish
		cmdBuf.end();
		queue->SubmitAndBlock(cmdBuf);

		cap->MarkModifiedFromGpu();
	}
	else
	#ifdef _APPLE_SILICON
		LogFatal("FFTFilter cpu fallback not available on Apple Silicon (yet?)");
	#else
	{
		data.PrepareForCpuAccess();
		m_rdinbuf.PrepareForCpuAccess();
		m_rdoutbuf.PrepareForCpuAccess();
		cap->PrepareForCpuAccess();

		//Copy the input with windowing, then zero pad to the desired input length if needed
		ApplyWindow(
			(float*)&data[0],
			numActualSamples,
			&m_rdinbuf[0],
			window);
		if(npoints > m_cachedNumPoints)
			memset(&m_rdinbuf[m_cachedNumPoints], 0, (npoints - m_cachedNumPoints) * sizeof(float));

		//Calculate the FFT
		ffts_execute(m_plan, &m_rdinbuf[0], &m_rdoutbuf[0]);

		//Normalize magnitudes
		if(log_output)
		{
			#ifdef __x86_64__
			if(g_hasAvx2 && g_hasFMA)
				NormalizeOutputLogAVX2FMA(cap->m_samples, nouts, scale);
			else
			#endif
				NormalizeOutputLog(cap->m_samples, nouts, scale);
		}
		else
		{
			#ifdef __x86_64__
			if(g_hasAvx2)
				NormalizeOutputLinearAVX2(cap->m_samples, nouts, scale);
			else
			#endif
				NormalizeOutputLinear(cap->m_samples, nouts, scale);
		}

		cap->MarkModifiedFromCpu();
	}
	#endif

	//Peak search
	FindPeaks(cap);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Normalization

/**
	@brief Normalize FFT output and convert to dBm (unoptimized C++ implementation)
 */
void FFTFilter::NormalizeOutputLog(AcceleratorBuffer<float>& data, size_t nouts, float scale)
{
	//assume constant 50 ohms for now
	const float impedance = 50;
	const float sscale = scale*scale / impedance;
	for(size_t i=0; i<nouts; i++)
	{
		float real = m_rdoutbuf[i*2];
		float imag = m_rdoutbuf[i*2 + 1];

		float vsq = real*real + imag*imag;

		//Convert to dBm
		data[i] = (10 * log10(vsq * sscale) + 30);
	}
}

/**
	@brief Normalize FFT output and output in native Y-axis units (unoptimized C++ implementation)
 */
void FFTFilter::NormalizeOutputLinear(AcceleratorBuffer<float>& data, size_t nouts, float scale)
{
	for(size_t i=0; i<nouts; i++)
	{
		float real = m_rdoutbuf[i*2];
		float imag = m_rdoutbuf[i*2 + 1];

		data[i] = sqrtf(real*real + imag*imag) * scale;
	}
}

#ifdef __x86_64__
/**
	@brief Normalize FFT output and convert to dBm (optimized AVX2 implementation)
 */
__attribute__((target("avx2,fma")))
void FFTFilter::NormalizeOutputLogAVX2FMA(AcceleratorBuffer<float>& data, size_t nouts, float scale)
{
	size_t end = nouts - (nouts % 8);

	//double since we only look at positive half
	float impedance = 50;
	const float sscale = scale*scale / impedance;
	__m256 vscale = { sscale, sscale, sscale, sscale, sscale, sscale, sscale, sscale };

	//Constant values for dBm conversion
	const float logscale = 10 / log(10);
	__m256 vlogscale = { logscale, logscale, logscale, logscale, logscale, logscale, logscale, logscale };
	__m256 const_30 = {30, 30, 30, 30, 30, 30, 30, 30 };

	float* pout = data.GetCpuPointer();
	float* pin = &m_rdoutbuf[0];

	//Vectorized processing (8 samples per iteration)
	for(size_t k=0; k<end; k += 8)
	{
		//Read interleaved real/imaginary FFT output (riririri riririri)
		__m256 din0 = _mm256_load_ps(pin + k*2);
		__m256 din1 = _mm256_load_ps(pin + k*2 + 8);

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
		__m256 vsq = _mm256_add_ps(real, imag);
		vsq = _mm256_mul_ps(vsq, vscale);

		//Convert to dBm
		vsq = _mm256_log_ps(vsq);
		__m256 dbm = _mm256_fmadd_ps(vsq, vlogscale, const_30);

		//and store the actual output
		_mm256_store_ps(pout + k, dbm);
	}

	//Get any extras we didn't get in the SIMD loop
	for(size_t k=end; k<nouts; k++)
	{
		float real = m_rdoutbuf[k*2];
		float imag = m_rdoutbuf[k*2 + 1];

		float vsq = real*real + imag*imag;
		float dbm = (logscale * log(vsq * sscale) + 30);

		pout[k] = dbm;
	}
}

/**
	@brief Normalize FFT output and keep in native units (optimized AVX2 implementation)
 */
__attribute__((target("avx2")))
void FFTFilter::NormalizeOutputLinearAVX2(AcceleratorBuffer<float>& data, size_t nouts, float scale)
{
	size_t end = nouts - (nouts % 8);

	//double since we only look at positive half
	__m256 norm_f = { scale, scale, scale, scale, scale, scale, scale, scale };

	float* pout = data.GetCpuPointer();
	float* pin = &m_rdoutbuf[0];

	//Vectorized processing (8 samples per iteration)
	for(size_t k=0; k<end; k += 8)
	{
		//Read interleaved real/imaginary FFT output (riririri riririri)
		__m256 din0 = _mm256_load_ps(pin + k*2);
		__m256 din1 = _mm256_load_ps(pin + k*2 + 8);

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
		float real = m_rdoutbuf[k*2];
		float imag = m_rdoutbuf[k*2 + 1];

		pout[k] = sqrtf(real*real + imag*imag) * scale;
	}
}
#endif /* __x86_64__ */

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Window functions

void FFTFilter::ApplyWindow(const float* data, size_t len, float* out, WindowFunction func)
{
	switch(func)
	{
		case WINDOW_BLACKMAN_HARRIS:
			#ifdef __x86_64__
			if(g_hasAvx2)
				return BlackmanHarrisWindowAVX2(data, len, out);
			else
			#endif
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

#ifdef __x86_64__
__attribute__((target("avx2")))
void FFTFilter::CosineSumWindowAVX2(const float* data, size_t len, float* out, float alpha0)
{
	float alpha1 = 1 - alpha0;
	float scale = 2.0f * (float)M_PI / len;

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

		__m256 din		= _mm256_loadu_ps(data + i);
		__m256 dout		= _mm256_mul_ps(din, w);
		_mm256_storeu_ps(out + i, dout);

		count_x8 = _mm256_add_ps(count_x8, eights_x8);
	}

	//Last few iterations
	for(; i<len; i++)
	{
		float w = alpha0 - alpha1*cosf(i*scale);
		out[i] = w * data[i];
	}
}
#endif /* __x86_64__ */

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

#ifdef __x86_64__
__attribute__((target("avx2")))
void FFTFilter::BlackmanHarrisWindowAVX2(const float* data, size_t len, float* out)
{
	float alpha0 = 0.35875;
	float alpha1 = 0.48829;
	float alpha2 = 0.14128;
	float alpha3 = 0.01168;
	float scale = 2 *(float)M_PI / len;

	__m256 count_x8		= { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f };
	__m256 eights_x8	= { 8.0f, 8.0f, 8.0f, 8.0f, 8.0f, 8.0f, 8.0f, 8.0f };
	__m256 scale_x8		= { scale, scale, scale, scale, scale, scale, scale, scale };
	__m256 alpha0_x8	= { alpha0, alpha0, alpha0, alpha0, alpha0, alpha0, alpha0, alpha0 };
	__m256 alpha1_x8	= { alpha1, alpha1, alpha1, alpha1, alpha1, alpha1, alpha1, alpha1 };
	__m256 alpha2_x8	= { alpha2, alpha2, alpha2, alpha2, alpha2, alpha2, alpha2, alpha2 };
	__m256 alpha3_x8	= { alpha3, alpha3, alpha3, alpha3, alpha3, alpha3, alpha3, alpha3 };
	__m256 two_x8		= { 2, 2, 2, 2, 2, 2, 2, 2 };
	__m256 six_x8		= { 6, 6, 6, 6, 6, 6, 6, 6 };

	size_t i=0;
	size_t len_rounded = len - (len % 8);
	for(; i<len_rounded; i += 8)
	{
		__m256 vscale		= _mm256_mul_ps(count_x8, scale_x8);
		__m256 vscale_x2	= _mm256_mul_ps(vscale, two_x8);
		__m256 vscale_x6	= _mm256_mul_ps(vscale, six_x8);

		__m256 term1		= _mm256_cos_ps(vscale);
		__m256 term2		= _mm256_cos_ps(vscale_x2);
		__m256 term3		= _mm256_cos_ps(vscale_x6);
		term1 				= _mm256_mul_ps(term1, alpha1_x8);
		term2 				= _mm256_mul_ps(term2, alpha2_x8);
		term3 				= _mm256_mul_ps(term3, alpha3_x8);
		__m256 w			= _mm256_sub_ps(alpha0_x8, term1);
		w					= _mm256_add_ps(w, term2);
		w					= _mm256_sub_ps(w, term3);

		__m256 din			= _mm256_loadu_ps(data + i);
		__m256 dout			= _mm256_mul_ps(din, w);
		_mm256_storeu_ps(out + i, dout);

		count_x8 = _mm256_add_ps(count_x8, eights_x8);
	}

	for(; i<len; i++)
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
#endif /* __x86_64__ */

void FFTFilter::HannWindow(const float* data, size_t len, float* out)
{
	#ifdef __x86_64__
	if(g_hasAvx2)
		CosineSumWindowAVX2(data, len, out, 0.5);
	else
	#endif
		CosineSumWindow(data, len, out, 0.5);
}

void FFTFilter::HammingWindow(const float* data, size_t len, float* out)
{
	#ifdef __x86_64__
	if(g_hasAvx2)
		CosineSumWindowAVX2(data, len, out, 25.0f / 46);
	else
	#endif
		CosineSumWindow(data, len, out, 25.0f / 46);
}
