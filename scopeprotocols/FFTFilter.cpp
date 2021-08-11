/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2021 Andrew D. Zonenberg and contributors                                                         *
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

	#ifdef HAVE_CLFFT

		m_clfftPlan = 0;

		m_windowProgram = NULL;
		m_rectangularWindowKernel = NULL;
		m_cosineSumWindowKernel = NULL;
		m_blackmanHarrisWindowKernel = NULL;

		m_normalizeProgram = NULL;
		m_normalizeLogMagnitudeKernel = NULL;
		m_normalizeMagnitudeKernel = NULL;

		m_queue = NULL;

		try
		{
			//Important to check g_clContext - OpenCL enabled at compile time does not guarantee that we have any
			//usable OpenCL devices actually present on the system. We might also have disabled it via --noopencl.
			if(g_clContext)
			{
				m_queue = new cl::CommandQueue(*g_clContext, g_contextDevices[0], 0);

				//Compile window functions
				string kernelSource = ReadDataFile("kernels/WindowFunctions.cl");
				cl::Program::Sources sources(1, make_pair(&kernelSource[0], kernelSource.length()));
				m_windowProgram = new cl::Program(*g_clContext, sources);
				m_windowProgram->build(g_contextDevices);

				//Extract each kernel
				m_rectangularWindowKernel = new cl::Kernel(*m_windowProgram, "RectangularWindow");
				m_cosineSumWindowKernel = new cl::Kernel(*m_windowProgram, "CosineSumWindow");
				m_blackmanHarrisWindowKernel = new cl::Kernel(*m_windowProgram, "BlackmanHarrisWindow");

				//Compile normalization kernels
				kernelSource = ReadDataFile("kernels/FFTNormalization.cl");
				cl::Program::Sources sources2(1, make_pair(&kernelSource[0], kernelSource.length()));
				m_normalizeProgram = new cl::Program(*g_clContext, sources2);
				m_normalizeProgram->build(g_contextDevices);

				//Extract normalization kernels
				m_normalizeLogMagnitudeKernel = new cl::Kernel(*m_normalizeProgram, "NormalizeToLogMagnitude");
				m_normalizeMagnitudeKernel = new cl::Kernel(*m_normalizeProgram, "NormalizeToMagnitude");
			}
		}
		catch(const cl::Error& e)
		{
			LogError("OpenCL error: %s (%d)\n", e.what(), e.err() );

			if(e.err() == CL_BUILD_PROGRAM_FAILURE)
			{
				LogError("Failed to build OpenCL program for FFT\n");

				string log;
				if(m_windowProgram)
				{
					m_windowProgram->getBuildInfo<string>(g_contextDevices[0], CL_PROGRAM_BUILD_LOG, &log);
					LogDebug("Window program build log:\n");
					LogDebug("%s\n", log.c_str());
				}
				else
					LogDebug("Window program object not present\n");

				if(m_normalizeProgram)
				{
					m_normalizeProgram->getBuildInfo<string>(g_contextDevices[0], CL_PROGRAM_BUILD_LOG, &log);
					LogDebug("Normalize program build log:\n");
					LogDebug("%s\n", log.c_str());
				}
				else
					LogDebug("Normalize program object not present\n");
			}

			delete m_windowProgram;
			delete m_rectangularWindowKernel;
			delete m_cosineSumWindowKernel;
			delete m_blackmanHarrisWindowKernel;

			delete m_normalizeProgram;
			delete m_normalizeLogMagnitudeKernel;
			delete m_normalizeMagnitudeKernel;

			m_windowProgram = NULL;
			m_rectangularWindowKernel = NULL;
			m_cosineSumWindowKernel = NULL;
			m_blackmanHarrisWindowKernel = NULL;

			m_normalizeProgram = NULL;
			m_normalizeLogMagnitudeKernel = NULL;
			m_normalizeMagnitudeKernel = NULL;
		}

	#endif
}

FFTFilter::~FFTFilter()
{
	if(m_plan)
		ffts_free(m_plan);

	#ifdef HAVE_CLFFT
		if(m_clfftPlan != 0)
			clfftDestroyPlan(&m_clfftPlan);

		delete m_windowProgram;
		delete m_rectangularWindowKernel;
		delete m_cosineSumWindowKernel;
		delete m_blackmanHarrisWindowKernel;

		m_windowProgram = NULL;
		m_rectangularWindowKernel = NULL;
		m_cosineSumWindowKernel = NULL;
		m_blackmanHarrisWindowKernel = NULL;

		delete m_queue;

	#endif
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
		{
			ffts_free(m_plan);

			#ifdef HAVE_CLFFT
				if(m_clfftPlan != 0)
					clfftDestroyPlan(&m_clfftPlan);
			#endif
		}

		m_plan = ffts_init_1d_real(npoints, FFTS_FORWARD);

		#ifdef HAVE_CLFFT

			if(g_clContext)
			{
				//Set up the FFT object
				if(CLFFT_SUCCESS != clfftCreateDefaultPlan(&m_clfftPlan, (*g_clContext)(), CLFFT_1D, &npoints))
				{
					LogError("clfftCreateDefaultPlan failed\n");
					abort();
				}
				clfftSetPlanBatchSize(m_clfftPlan, 1);
				clfftSetPlanPrecision(m_clfftPlan, CLFFT_SINGLE);
				clfftSetLayout(m_clfftPlan, CLFFT_REAL, CLFFT_HERMITIAN_INTERLEAVED);
				clfftSetResultLocation(m_clfftPlan, CLFFT_OUTOFPLACE);

				//Initialize the plan
				cl_command_queue q = (*m_queue)();
				auto err = clfftBakePlan(m_clfftPlan, 1, &q, NULL, NULL);
				if(CLFFT_SUCCESS != err)
				{
					LogError("clfftBakePlan failed (%d)\n", err);
					abort();
				}
			}

		#endif
	}

	m_rdinbuf.resize(npoints);
	m_rdoutbuf.resize(2*nouts);
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

	double fs = din->m_timescale * (din->m_offsets[1] - din->m_offsets[0]);
	DoRefresh(din, din->m_samples, fs, npoints, nouts, true);
}

void FFTFilter::DoRefresh(
	AnalogWaveform* din,
	vector<EmptyConstructorWrapper<float>, AlignedAllocator<EmptyConstructorWrapper<float>, 64>>& data,
	double fs_per_sample,
	size_t npoints,
	size_t nouts,
	bool log_output)
{
	//Look up some parameters
	float scale = 2.0 / npoints;
	double sample_ghz = 1e6 / fs_per_sample;
	double bin_hz = round((0.5f * sample_ghz * 1e9f) / nouts);
	auto window = static_cast<WindowFunction>(m_parameters[m_windowName].GetIntVal());
	LogTrace("bin_hz: %f\n", bin_hz);

	//Set up output and copy time scales / configuration
	AnalogWaveform* cap = dynamic_cast<AnalogWaveform*>(GetData(0));
	if(cap == NULL)
	{
		cap = new AnalogWaveform;
		SetData(cap, 0);
	}
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	cap->m_triggerPhase = 0;
	cap->m_timescale = bin_hz;
	cap->m_densePacked = true;

	//Update output timestamps if capture depth grew
	size_t oldlen = cap->m_offsets.size();
	cap->Resize(nouts);
	if(nouts > oldlen)
	{
		for(size_t i = oldlen; i < nouts; i++)
		{
			cap->m_offsets[i] = i;
			cap->m_durations[i] = 1;
		}
	}

	#ifdef HAVE_CLFFT
		if(g_clContext && m_windowProgram && m_normalizeProgram)
		{
			try
			{
				//Make buffers
				cl::Buffer inbuf(*m_queue, data.begin(), data.end(), true, true, NULL);
				cl::Buffer windowoutbuf(*g_clContext, CL_MEM_READ_WRITE, sizeof(float) * npoints);
				cl::Buffer fftoutbuf(*g_clContext, CL_MEM_READ_WRITE, sizeof(float) * 2 * nouts);
				cl::Buffer outbuf(*m_queue, cap->m_samples.begin(), cap->m_samples.end(), false, true, NULL);

				//Apply the window function
				cl::Kernel* windowKernel = NULL;
				float windowscale = 2 * M_PI / m_cachedNumPoints;
				switch(window)
				{
					case WINDOW_RECTANGULAR:
						windowKernel = m_rectangularWindowKernel;
						break;

					case WINDOW_HAMMING:
						windowKernel = m_cosineSumWindowKernel;
						windowKernel->setArg(4, 25.0f / 46.0f);
						windowKernel->setArg(5, 1.0f - (25.0f / 46.0f));
						break;

					case WINDOW_HANN:
						windowKernel = m_cosineSumWindowKernel;
						windowKernel->setArg(4, 0.5);
						windowKernel->setArg(5, 0.5);
						break;

					case WINDOW_BLACKMAN_HARRIS:
					default:
						windowKernel = m_blackmanHarrisWindowKernel;
						break;
				}
				windowKernel->setArg(0, inbuf);
				windowKernel->setArg(1, windowoutbuf);
				windowKernel->setArg(2, data.size());
				if(window != WINDOW_RECTANGULAR)
					windowKernel->setArg(3, windowscale);
				m_queue->enqueueNDRangeKernel(*windowKernel, cl::NullRange, cl::NDRange(npoints, 1), cl::NullRange, NULL);

				//Run the FFT
				cl_command_queue q = (*m_queue)();
				cl_mem inbufs[1] = { windowoutbuf() };
				cl_mem outbufs[1] = { fftoutbuf() };
				if(CLFFT_SUCCESS != clfftEnqueueTransform(
					m_clfftPlan, CLFFT_FORWARD, 1, &q, 0, NULL, NULL, inbufs, outbufs, NULL) )
				{
					LogError("clfftEnqueueTransform failed\n");
					abort();
				}

				//Normalize output
				cl::Kernel* normalizeKernel = NULL;
				if(log_output)
					normalizeKernel = m_normalizeLogMagnitudeKernel;
				else
					normalizeKernel = m_normalizeMagnitudeKernel;
				normalizeKernel->setArg(0, fftoutbuf);
				normalizeKernel->setArg(1, outbuf);
				normalizeKernel->setArg(2, scale);
				m_queue->enqueueNDRangeKernel(
					*normalizeKernel, cl::NullRange, cl::NDRange(nouts, 1), cl::NullRange, NULL);

				//Map/unmap the buffer to synchronize output with the CPU
				void* ptr = m_queue->enqueueMapBuffer(outbuf, true, CL_MAP_READ, 0, nouts * sizeof(float));
				m_queue->enqueueUnmapMemObject(outbuf, ptr);
			}
			catch(const cl::Error& e)
			{
				LogFatal("OpenCL error: %s (%d)\n", e.what(), e.err() );
			}
		}
		else
		{
	#endif

		//Copy the input with windowing, then zero pad to the desired input length
		ApplyWindow(
			(float*)&data[0],
			m_cachedNumPoints,
			&m_rdinbuf[0],
			window);
		memset(&m_rdinbuf[m_cachedNumPoints], 0, (npoints - m_cachedNumPoints) * sizeof(float));

		//Calculate the FFT
		ffts_execute(m_plan, &m_rdinbuf[0], &m_rdoutbuf[0]);

		//Normalize magnitudes
		if(log_output)
		{
			if(g_hasAvx2)
				NormalizeOutputLogAVX2(cap, nouts, scale);
			else
				NormalizeOutputLog(cap, nouts, scale);
		}
		else
		{
			if(g_hasAvx2)
				NormalizeOutputLinearAVX2(cap, nouts, scale);
			else
				NormalizeOutputLinear(cap, nouts, scale);
		}

	#ifdef HAVE_CLFFT
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
void FFTFilter::NormalizeOutputLog(AnalogWaveform* cap, size_t nouts, float scale)
{
	//assume constant 50 ohms for now
	const float impedance = 50;
	for(size_t i=0; i<nouts; i++)
	{
		float real = m_rdoutbuf[i*2];
		float imag = m_rdoutbuf[i*2 + 1];

		float voltage = sqrtf(real*real + imag*imag) * scale;

		//Convert to dBm
		cap->m_samples[i] = (10 * log10(voltage*voltage / impedance) + 30);
	}
}

/**
	@brief Normalize FFT output and output in native Y-axis units (unoptimized C++ implementation)
 */
void FFTFilter::NormalizeOutputLinear(AnalogWaveform* cap, size_t nouts, float scale)
{
	for(size_t i=0; i<nouts; i++)
	{
		float real = m_rdoutbuf[i*2];
		float imag = m_rdoutbuf[i*2 + 1];

		cap->m_samples[i] = sqrtf(real*real + imag*imag) * scale;
	}
}

/**
	@brief Normalize FFT output and convert to dBm (optimized AVX2 implementation)
 */
__attribute__((target("avx2")))
void FFTFilter::NormalizeOutputLogAVX2(AnalogWaveform* cap, size_t nouts, float scale)
{
	size_t end = nouts - (nouts % 8);

	//double since we only look at positive half
	__m256 norm_f = { scale, scale, scale, scale, scale, scale, scale, scale };

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
		float real = m_rdoutbuf[k*2];
		float imag = m_rdoutbuf[k*2 + 1];

		float voltage = sqrtf(real*real + imag*imag) * scale;

		//Convert to dBm
		pout[k] = (10 * log10(voltage*voltage / impedance) + 30);
	}
}

/**
	@brief Normalize FFT output and keep in native units (optimized AVX2 implementation)
 */
__attribute__((target("avx2")))
void FFTFilter::NormalizeOutputLinearAVX2(AnalogWaveform* cap, size_t nouts, float scale)
{
	size_t end = nouts - (nouts % 8);

	//double since we only look at positive half
	__m256 norm_f = { scale, scale, scale, scale, scale, scale, scale, scale };

	float* pout = (float*)&cap->m_samples[0];
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
