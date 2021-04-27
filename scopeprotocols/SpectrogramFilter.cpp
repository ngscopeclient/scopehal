/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2021 Andrew D. Zonenberg                                                                          *
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
#include "SpectrogramFilter.h"
#include <immintrin.h>
#include "../scopehal/avx_mathfun.h"
#include "FFTFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SpectrogramWaveform::SpectrogramWaveform(size_t width, size_t height, float fmax, int64_t tstart, int64_t duration)
	: m_width(width)
	, m_height(height)
	, m_fmax(fmax)
	, m_tstart(tstart)
	, m_duration(duration)
{
	size_t npix = width*height;
	m_data = new float[npix];
	for(size_t i=0; i<npix; i++)
		m_data[i] = 0;
}

SpectrogramWaveform::~SpectrogramWaveform()
{
	delete[] m_data;
	m_data = NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SpectrogramFilter::SpectrogramFilter(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_SPECTROGRAM, color, CAT_RF)
	, m_windowName("Window")
	, m_fftLengthName("FFT length")
{
	m_yAxisUnit = Unit(Unit::UNIT_HZ);

	//Set up channels
	CreateInput("din");
	m_plan = NULL;

	//Default config
	m_range = 1e9;
	m_offset = -5e8;
	m_cachedFFTLength = 0;

	m_parameters[m_windowName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_windowName].AddEnumValue("Blackman-Harris", FFTFilter::WINDOW_BLACKMAN_HARRIS);
	m_parameters[m_windowName].AddEnumValue("Hamming", FFTFilter::WINDOW_HAMMING);
	m_parameters[m_windowName].AddEnumValue("Hann", FFTFilter::WINDOW_HANN);
	m_parameters[m_windowName].AddEnumValue("Rectangular", FFTFilter::WINDOW_RECTANGULAR);
	m_parameters[m_windowName].SetIntVal(FFTFilter::WINDOW_BLACKMAN_HARRIS);

	m_parameters[m_fftLengthName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_SAMPLEDEPTH));
	m_parameters[m_fftLengthName].AddEnumValue("512", 512);
	m_parameters[m_fftLengthName].AddEnumValue("1024", 1024);
	m_parameters[m_fftLengthName].AddEnumValue("2048", 2048);
	m_parameters[m_fftLengthName].AddEnumValue("4096", 4096);
	m_parameters[m_fftLengthName].SetIntVal(512);
}

SpectrogramFilter::~SpectrogramFilter()
{
	if(m_plan)
		ffts_free(m_plan);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool SpectrogramFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

double SpectrogramFilter::GetOffset()
{
	return m_offset;
}

double SpectrogramFilter::GetVoltageRange()
{
	return m_range;
}

void SpectrogramFilter::SetVoltageRange(double range)
{
	m_range = range;
}

void SpectrogramFilter::SetOffset(double offset)
{
	m_offset = offset;
}

string SpectrogramFilter::GetProtocolName()
{
	return "Spectrogram";
}

bool SpectrogramFilter::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool SpectrogramFilter::NeedsConfig()
{
	return false;
}

void SpectrogramFilter::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "Spectrogram(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void SpectrogramFilter::ReallocateBuffers(size_t fftlen)
{
	m_cachedFFTLength = fftlen;
	ffts_free(m_plan);
	m_plan = ffts_init_1d_real(fftlen, FFTS_FORWARD);

	m_rdinbuf.resize(fftlen);
	m_rdoutbuf.resize(2*fftlen);
}

void SpectrogramFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}
	auto din = GetAnalogInputWaveform(0);

	//Figure out how many FFTs to do
	//For now, consecutive blocks and not a sliding window
	size_t inlen = din->m_samples.size();
	size_t fftlen = m_parameters[m_fftLengthName].GetIntVal();
	if(fftlen != m_cachedFFTLength)
		ReallocateBuffers(fftlen);
	size_t nblocks = inlen / fftlen;

	//Figure out range of the FFTs
	double fs_per_sample = din->m_timescale * (din->m_offsets[1] - din->m_offsets[0]);
	float scale = 2.0 / fftlen;
	double sample_ghz = 1e6 / fs_per_sample;
	double bin_hz = round((0.5f * sample_ghz * 1e9f) / fftlen);
	double fmax = bin_hz * fftlen;

	Unit hz(Unit::UNIT_HZ);
	LogDebug("SpectrogramFilter: %zu input points, %zu %zu-point FFTs\n", inlen, nblocks, fftlen);
	LogIndenter li;
	LogDebug("FFT range is DC to %s\n", hz.PrettyPrint(fmax).c_str());
	LogDebug("%s per bin\n", hz.PrettyPrint(bin_hz).c_str());

	//Create the output
	auto spec = new SpectrogramWaveform(
		nblocks,
		fftlen,
		fmax,
		din->m_offsets[0] * din->m_timescale,
		fs_per_sample * nblocks * fftlen
		);
	SetData(spec, 0);

	//Fill the dummy output with a checkerboard pattern
	auto data = spec->GetData();
	for(size_t x=0; x<nblocks; x++)
	{
		for(size_t y=0; y<fftlen; y++)
		{
			if( (y ^ x) & 1)
				data[y*nblocks + x] = 0.75;
			else
				data[y*nblocks + x] = 0.25;
		}
	}

	/*

	//Round size up to next power of two
	const size_t npoints_raw = din->m_samples.size();
	const size_t npoints = next_pow2(npoints_raw);
	LogTrace("SpectrogramFilter: processing %zu raw points\n", npoints_raw);
	LogTrace("Rounded to %zu\n", npoints);

	//Reallocate buffers if size has changed
	const size_t nouts = npoints/2 + 1;
	if(m_cachedNumPoints != npoints_raw)
		ReallocateBuffers(npoints_raw, npoints, nouts);
	LogTrace("Output: %zu\n", nouts);

	double fs = din->m_timescale * (din->m_offsets[1] - din->m_offsets[0]);
	DoRefresh(din, din->m_samples, fs, npoints, nouts, true);
	*/
}

/*
void SpectrogramFilter::DoRefresh(
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

	#ifdef HAVE_CLSpectrogram
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
				windowKernel->setArg(2, m_cachedNumPoints);
				if(window != WINDOW_RECTANGULAR)
					windowKernel->setArg(3, windowscale);
				m_queue->enqueueNDRangeKernel(*windowKernel, cl::NullRange, cl::NDRange(npoints, 1), cl::NullRange, NULL);

				//Run the Spectrogram
				cl_command_queue q = (*m_queue)();
				cl_mem inbufs[1] = { windowoutbuf() };
				cl_mem outbufs[1] = { fftoutbuf() };
				if(CLSpectrogram_SUCCESS != clfftEnqueueTransform(
					m_clfftPlan, CLSpectrogram_FORWARD, 1, &q, 0, NULL, NULL, inbufs, outbufs, NULL) )
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

		//Calculate the Spectrogram
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

	#ifdef HAVE_CLSpectrogram
		}
	#endif

	//Peak search
	FindPeaks(cap);
}
*/

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Normalization

/**
	@brief Normalize Spectrogram output and convert to dBm (unoptimized C++ implementation)
 */
 /*
void SpectrogramFilter::NormalizeOutputLog(AnalogWaveform* cap, size_t nouts, float scale)
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
*/

/**
	@brief Normalize Spectrogram output and output in native Y-axis units (unoptimized C++ implementation)
 */
 /*
void SpectrogramFilter::NormalizeOutputLinear(AnalogWaveform* cap, size_t nouts, float scale)
{
	for(size_t i=0; i<nouts; i++)
	{
		float real = m_rdoutbuf[i*2];
		float imag = m_rdoutbuf[i*2 + 1];

		cap->m_samples[i] = sqrtf(real*real + imag*imag) * scale;
	}
}*/

/**
	@brief Normalize Spectrogram output and convert to dBm (optimized AVX2 implementation)
 */
 /*
__attribute__((target("avx2")))
void SpectrogramFilter::NormalizeOutputLogAVX2(AnalogWaveform* cap, size_t nouts, float scale)
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
		//Read interleaved real/imaginary Spectrogram output (riririri riririri)
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
}*/

/**
	@brief Normalize Spectrogram output and keep in native units (optimized AVX2 implementation)
 */
 /*
__attribute__((target("avx2")))
void SpectrogramFilter::NormalizeOutputLinearAVX2(AnalogWaveform* cap, size_t nouts, float scale)
{
	size_t end = nouts - (nouts % 8);

	//double since we only look at positive half
	__m256 norm_f = { scale, scale, scale, scale, scale, scale, scale, scale };

	float* pout = (float*)&cap->m_samples[0];
	float* pin = &m_rdoutbuf[0];

	//Vectorized processing (8 samples per iteration)
	for(size_t k=0; k<end; k += 8)
	{
		//Read interleaved real/imaginary Spectrogram output (riririri riririri)
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
*/
