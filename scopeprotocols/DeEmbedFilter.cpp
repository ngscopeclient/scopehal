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
#include "DeEmbedFilter.h"
#include <immintrin.h>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DeEmbedFilter::DeEmbedFilter(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_ANALYSIS)
{
	//Set up channels
	CreateInput("din");

	m_fname = "S-Parameters";
	m_parameters[m_fname] = FilterParameter(FilterParameter::TYPE_FILENAMES, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_fname].m_fileFilterMask = "*.s2p";
	m_parameters[m_fname].m_fileFilterName = "Touchstone S-parameter files (*.s2p)";

	m_range = 1;
	m_offset = 0;
	m_min = FLT_MAX;
	m_max = -FLT_MAX;
	m_cachedBinSize = 0;

	m_forwardPlan = NULL;
	m_reversePlan = NULL;

	m_cachedNumPoints = 0;
	m_cachedRawSize = 0;

	#ifdef HAVE_CLFFT

		m_windowProgram = NULL;

		try
		{
			//Compile window functions
			string kernelSource = ReadFile("kernels/WindowFunctions.cl");
			cl::Program::Sources sources(1, make_pair(&kernelSource[0], kernelSource.length()));
			m_windowProgram = new cl::Program(*g_clContext, sources);
			m_windowProgram->build(g_contextDevices);

			//Extract each kernel
			m_rectangularWindowKernel = new cl::Kernel(*m_windowProgram, "RectangularWindow");
		}
		catch(const cl::Error& e)
		{
			LogError("OpenCL error: %s (%d)\n", e.what(), e.err() );

			if(e.err() == CL_BUILD_PROGRAM_FAILURE)
			{
				LogError("Failed to build OpenCL program for FFT\n");

				string log;
				m_windowProgram->getBuildInfo<string>(g_contextDevices[0], CL_PROGRAM_BUILD_LOG, &log);
				LogDebug("Window program build log:\n");
				LogDebug("%s\n", log.c_str());
			}

			delete m_windowProgram;
			delete m_rectangularWindowKernel;

			m_windowProgram = NULL;
			m_rectangularWindowKernel = NULL;
		}

	#endif
}

DeEmbedFilter::~DeEmbedFilter()
{
	if(m_forwardPlan)
		ffts_free(m_forwardPlan);
	if(m_reversePlan)
		ffts_free(m_reversePlan);

	m_forwardPlan = NULL;
	m_reversePlan = NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DeEmbedFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

double DeEmbedFilter::GetVoltageRange()
{
	return m_range;
}

double DeEmbedFilter::GetOffset()
{
	return m_offset;
}

string DeEmbedFilter::GetProtocolName()
{
	return "De-Embed";
}

bool DeEmbedFilter::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool DeEmbedFilter::NeedsConfig()
{
	//we need the offset to be specified, duh
	return true;
}

void DeEmbedFilter::SetDefaultName()
{
	vector<string> fnames = m_parameters[m_fname].GetFileNames();
	string base;
	for(auto f : fnames)
	{
		if(base != "")
			base += ", ";
		base += BaseName(f);
	}

	char hwname[256];
	snprintf(
		hwname,
		sizeof(hwname),
		"DeEmbed(%s, %s)",
		GetInputDisplayName(0).c_str(),
		base.c_str()
		);

	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DeEmbedFilter::Refresh()
{
	DoRefresh(true);
}

void DeEmbedFilter::ClearSweeps()
{
	m_range = 1;
	m_offset = 0;
	m_min = FLT_MAX;
	m_max = -FLT_MAX;
}

bool DeEmbedFilter::LoadSparameters()
{
	//Reload the S-parameters from the Touchstone file(s) if the filename has changed
	vector<string> fnames = m_parameters[m_fname].GetFileNames();
	if(fnames != m_cachedFileNames)
	{
		m_cachedFileNames = fnames;

		m_sparams.Clear();
		TouchstoneParser parser;
		SParameters temp;
		for(auto f : fnames)
		{
			if(!parser.Load(f, temp))
				return false;

			m_sparams *= temp;
		}

		//Clear out cached S-parameters
		m_cachedBinSize = 0;
		m_resampledSparamCosines.clear();
		m_resampledSparamSines.clear();
		m_resampledSparamAmplitudes.clear();
	}

	//Don't die if the file couldn't be loaded
	if(m_sparams.empty())
		return false;

	return true;
}

/**
	@brief Applies the S-parameters in the forward or reverse direction
 */
void DeEmbedFilter::DoRefresh(bool invert)
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	if(!LoadSparameters())
	{
		SetData(NULL, 0);
		return;
	}

	double start = GetTime();

	auto din = GetAnalogInputWaveform(0);
	const size_t npoints_raw = din->m_samples.size();

	//Zero pad to next power of two up
	const size_t npoints = next_pow2(npoints_raw);
	//LogTrace("DeEmbedFilter: processing %zu raw points\n", npoints_raw);
	//LogTrace("Rounded to %zu\n", npoints);

	//Format the input data as raw samples for the FFT
	//TODO: handle non-uniform sample rates and resample?
	size_t nouts = npoints/2 + 1;

	//Set up the FFT and allocate buffers if we change point count
	bool sizechange = false;
	if( (m_cachedNumPoints != npoints) || (m_cachedRawSize != npoints_raw) )
	{
		if(m_forwardPlan)
			ffts_free(m_forwardPlan);
		m_forwardPlan = ffts_init_1d_real(npoints, FFTS_FORWARD);

		if(m_reversePlan)
			ffts_free(m_reversePlan);
		m_reversePlan = ffts_init_1d_real(npoints, FFTS_BACKWARD);

		m_forwardInBuf.resize(npoints);
		m_forwardOutBuf.resize(2 * nouts);
		m_reverseOutBuf.resize(npoints);

		m_cachedNumPoints = npoints;
		m_cachedRawSize = npoints_raw;
		sizechange = true;

		#ifdef HAVE_CLFFT

			if(g_clContext)
			{
				//Set up the FFT object
				if(CLFFT_SUCCESS != clfftCreateDefaultPlan(&m_clfftForwardPlan, (*g_clContext)(), CLFFT_1D, &npoints))
				{
					LogError("clfftCreateDefaultPlan failed\n");
					abort();
				}
				clfftSetPlanBatchSize(m_clfftForwardPlan, 1);
				clfftSetPlanPrecision(m_clfftForwardPlan, CLFFT_SINGLE);
				clfftSetLayout(m_clfftForwardPlan, CLFFT_REAL, CLFFT_HERMITIAN_INTERLEAVED);
				clfftSetResultLocation(m_clfftForwardPlan, CLFFT_OUTOFPLACE);

				if(CLFFT_SUCCESS != clfftCreateDefaultPlan(&m_clfftReversePlan, (*g_clContext)(), CLFFT_1D, &npoints))
				{
					LogError("clfftCreateDefaultPlan failed\n");
					abort();
				}
				clfftSetPlanBatchSize(m_clfftReversePlan, 1);
				clfftSetPlanPrecision(m_clfftReversePlan, CLFFT_SINGLE);
				clfftSetLayout(m_clfftReversePlan, CLFFT_HERMITIAN_INTERLEAVED, CLFFT_REAL);
				clfftSetResultLocation(m_clfftReversePlan, CLFFT_OUTOFPLACE);
				clfftSetPlanScale(m_clfftReversePlan, CLFFT_BACKWARD, 1);

				//Initialize the plan
				cl::CommandQueue queue(*g_clContext, g_contextDevices[0], 0);
				cl_command_queue q = queue();
				auto err = clfftBakePlan(m_clfftForwardPlan, 1, &q, NULL, NULL);
				if(CLFFT_SUCCESS != err)
				{
					LogError("clfftBakePlan failed (%d)\n", err);
					abort();
				}
				err = clfftBakePlan(m_clfftReversePlan, 1, &q, NULL, NULL);
				if(CLFFT_SUCCESS != err)
				{
					LogError("clfftBakePlan failed (%d)\n", err);
					abort();
				}
			}

		#endif
	}

	//Calculate size of each bin
	double fs = din->m_timescale * (din->m_offsets[1] - din->m_offsets[0]);
	double sample_ghz = 1e6 / fs;
	double bin_hz = round((0.5f * sample_ghz * 1e9f) / nouts);

	//Resample S21 to our FFT bin size if needed.
	//Cache trig function output because there's no AVX instructions for this.
	if( (fabs(m_cachedBinSize - bin_hz) > FLT_EPSILON) || sizechange )
	{
		m_resampledSparamCosines.clear();
		m_resampledSparamSines.clear();
		m_resampledSparamAmplitudes.clear();
		InterpolateSparameters(bin_hz, invert, nouts);
	}

	#ifdef HAVE_CLFFT
		if(g_clContext && m_windowProgram)
		{
			try
			{
				//Set up buffers
				cl::Buffer inbuf(*g_clContext, din->m_samples.begin(), din->m_samples.end(), true, true, NULL);
				cl::Buffer windowoutbuf(*g_clContext, CL_MEM_READ_WRITE, sizeof(float) * npoints);
				cl::Buffer fftoutbuf(*g_clContext, m_forwardOutBuf.begin(), m_forwardOutBuf.end(), false, true, NULL);
				cl::Buffer ifftoutbuf(*g_clContext, m_reverseOutBuf.begin(), m_reverseOutBuf.end(), false, true, NULL);

				//Copy and zero pad input
				cl::CommandQueue queue(*g_clContext, g_contextDevices[0], 0);
				m_rectangularWindowKernel->setArg(0, inbuf);
				m_rectangularWindowKernel->setArg(1, windowoutbuf);
				m_rectangularWindowKernel->setArg(2, m_cachedNumPoints);
				queue.enqueueNDRangeKernel(
					*m_rectangularWindowKernel, cl::NullRange, cl::NDRange(npoints, 1), cl::NullRange, NULL);

				//Do the FFT
				cl_command_queue q = queue();
				cl_mem inbufs[1] = { windowoutbuf() };
				cl_mem outbufs[1] = { fftoutbuf() };
				if(CLFFT_SUCCESS != clfftEnqueueTransform(
					m_clfftForwardPlan, CLFFT_FORWARD, 1, &q, 0, NULL, NULL, inbufs, outbufs, NULL) )
				{
					LogError("clfftEnqueueTransform failed\n");
					abort();
				}

				//Sync FFT output, then re-map for writing
				void* ptr = queue.enqueueMapBuffer(fftoutbuf, true, CL_MAP_READ, 0, 2 * nouts * sizeof(float));
				queue.enqueueUnmapMemObject(fftoutbuf, ptr);
				ptr = queue.enqueueMapBuffer(fftoutbuf, true, CL_MAP_WRITE, 0, 2 * nouts * sizeof(float));

				//Do the de-embed loop
				if(g_hasAvx2)
					MainLoopAVX2(nouts);
				else
					MainLoop(nouts);

				//Unmap so the GPU sees the updates
				queue.enqueueUnmapMemObject(fftoutbuf, ptr);

				//Do the inverse FFT
				inbufs[0] = fftoutbuf();
				outbufs[0] = ifftoutbuf();
				if(CLFFT_SUCCESS != clfftEnqueueTransform(
					m_clfftReversePlan, CLFFT_BACKWARD, 1, &q, 0, NULL, NULL, inbufs, outbufs, NULL) )
				{
					LogError("clfftEnqueueTransform failed\n");
					abort();
				}

				//Sync IFFT output
				ptr = queue.enqueueMapBuffer(ifftoutbuf, true, CL_MAP_READ, 0, npoints * sizeof(float));
				queue.enqueueUnmapMemObject(ifftoutbuf, ptr);
			}
			catch(const cl::Error& e)
			{
				LogFatal("OpenCL error: %s (%d)\n", e.what(), e.err() );
			}
		}
		else
		{
	#endif

		//Copy the input, then fill any extra space with zeroes
		memcpy(&m_forwardInBuf[0], &din->m_samples[0], npoints_raw*sizeof(float));
		for(size_t i=npoints_raw; i<npoints; i++)
			m_forwardInBuf[i] = 0;

		//Do the forward FFT
		ffts_execute(m_forwardPlan, &m_forwardInBuf[0], &m_forwardOutBuf[0]);

		//Do the actual filter operation
		if(g_hasAvx2)
			MainLoopAVX2(nouts);
		else
			MainLoop(nouts);

		//Calculate the inverse FFT
		ffts_execute(m_reversePlan, &m_forwardOutBuf[0], &m_reverseOutBuf[0]);

	#ifdef HAVE_CLFFT
		}
	#endif

	//Calculate maximum group delay for the first few S-parameter bins (approx propagation delay of the channel)
	int64_t groupdelay_fs = GetGroupDelay();
	int64_t groupdelay_samples = ceil( groupdelay_fs / din->m_timescale );

	//Calculate bounds for the *meaningful* output data.
	//Since we're phase shifting, there's gonna be some garbage response at one end of the channel.
	size_t istart = 0;
	size_t iend = npoints_raw;
	AnalogWaveform* cap = NULL;
	if(invert)
	{
		iend -= groupdelay_samples;
		cap = SetupOutputWaveform(din, 0, 0, groupdelay_samples);
	}
	else
	{
		istart += groupdelay_samples;
		cap = SetupOutputWaveform(din, 0, groupdelay_samples, 0);
	}

	//Apply phase shift for the group delay so we draw the waveform in the right place even if dense packed
	if(invert)
		cap->m_triggerPhase = -groupdelay_fs;
	else
		cap->m_triggerPhase = groupdelay_fs;

	//Copy waveform data after rescaling
	float scale = 1.0f / npoints;
	float vmin = FLT_MAX;
	float vmax = -FLT_MAX;
	size_t outlen = iend - istart;
	for(size_t i=0; i<outlen; i++)
	{
		float v = m_reverseOutBuf[i+istart] * scale;
		vmin = min(v, vmin);
		vmax = max(v, vmax);
		cap->m_samples[i] = v;
	}

	//Calculate bounds
	m_max = max(m_max, vmax);
	m_min = min(m_min, vmin);
	m_range = (m_max - m_min) * 1.05;
	m_offset = -( (m_max - m_min)/2 + m_min );

	double dt = GetTime() - start;
	static double ttotal = 0;
	static size_t ntotal = 0;
	ntotal ++;
	ttotal += dt;
	LogDebug("DeEmbedFilter: %f ms\n", ttotal * 1000 / ntotal);
}

int64_t DeEmbedFilter::GetGroupDelay()
{
	auto& s21 = m_sparams[SPair(2,1)];
	float max_delay = 0;
	for(size_t i=0; i<s21.size()-1 && i<50; i++)
		max_delay = max(max_delay, s21.GetGroupDelay(i));
	return max_delay * FS_PER_SECOND;
}

/**
	@brief Recalculate the cached S-parameters

	Since there's no AVX sin/cos instructions, precompute sin(phase) and cos(phase)
 */
void DeEmbedFilter::InterpolateSparameters(float bin_hz, bool invert, size_t nouts)
{
	m_cachedBinSize = bin_hz;

	for(size_t i=0; i<nouts; i++)
	{
		auto point = m_sparams.SamplePoint(2, 1, bin_hz * i);

		//De-embedding
		if(invert)
		{
			m_resampledSparamSines.push_back(sin(-point.m_phase));
			m_resampledSparamCosines.push_back(cos(-point.m_phase));

			if(fabs(point.m_amplitude) < FLT_EPSILON)
				m_resampledSparamAmplitudes.push_back(0);
			else
				m_resampledSparamAmplitudes.push_back(1.0f / point.m_amplitude);
		}

		//Channel emulation
		else
		{
			m_resampledSparamSines.push_back(sin(point.m_phase));
			m_resampledSparamCosines.push_back(cos(point.m_phase));
			m_resampledSparamAmplitudes.push_back(point.m_amplitude);
		}
	}
}

void DeEmbedFilter::MainLoop(size_t nouts)
{
	for(size_t i=0; i<nouts; i++)
	{
		float amplitude = m_resampledSparamAmplitudes[i];
		float cosval = m_resampledSparamSines[i];
		float sinval = m_resampledSparamCosines[i];

		//Uncorrected complex value
		float real_orig = m_forwardOutBuf[i*2 + 0];
		float imag_orig = m_forwardOutBuf[i*2 + 1];

		//Phase correction
		float real = real_orig*cosval - imag_orig*sinval;
		float imag = real_orig*sinval + imag_orig*cosval;

		//Amplitude correction
		m_forwardOutBuf[i*2 + 0] = real * amplitude;
		m_forwardOutBuf[i*2 + 1] = imag * amplitude;
	}
}

__attribute__((target("avx2")))
void DeEmbedFilter::MainLoopAVX2(size_t nouts)
{
	unsigned int end = nouts - (nouts % 8);

	//Vectorized loop doing 8 elements at once
	for(size_t i=0; i<end; i += 8)
	{
		//Load S-parameters
		//Precompute sin/cos since there's no AVX instruction to do this
		__m256 amplitude = _mm256_load_ps(&m_resampledSparamAmplitudes[i]);
		__m256 sinval = _mm256_load_ps(&m_resampledSparamSines[i]);
		__m256 cosval = _mm256_load_ps(&m_resampledSparamCosines[i]);

		//Load uncorrected complex values (interleaved real/imag real/imag)
		__m256 din0 = _mm256_load_ps(&m_forwardOutBuf[i*2]);
		__m256 din1 = _mm256_load_ps(&m_forwardOutBuf[i*2 + 8]);

		//Original state of each block is riririri.
		//Shuffle them around to get all the reals and imaginaries together.

		//Step 1: Shuffle 32-bit values within 128-bit lanes to get rriirrii rriirrii.
		din0 = _mm256_permute_ps(din0, 0xd8);
		din1 = _mm256_permute_ps(din1, 0xd8);

		//Step 2: Shuffle 64-bit values to get rrrriiii rrrriiii.
		__m256i block0 = _mm256_permute4x64_epi64(_mm256_castps_si256(din0), 0xd8);
		__m256i block1 = _mm256_permute4x64_epi64(_mm256_castps_si256(din1), 0xd8);

		//Step 3: Shuffle 128-bit values to get rrrrrrrr iiiiiiii.
		__m256 real = _mm256_castsi256_ps(_mm256_permute2x128_si256(block0, block1, 0x20));
		__m256 imag = _mm256_castsi256_ps(_mm256_permute2x128_si256(block0, block1, 0x31));

		//Create the sin/cos matrix
		__m256 real_sin = _mm256_mul_ps(real, sinval);
		__m256 real_cos = _mm256_mul_ps(real, cosval);
		__m256 imag_sin = _mm256_mul_ps(imag, sinval);
		__m256 imag_cos = _mm256_mul_ps(imag, cosval);

		//Do the phase correction
		real = _mm256_sub_ps(real_cos, imag_sin);
		imag = _mm256_add_ps(real_sin, imag_cos);

		//Amplitude correction
		real = _mm256_mul_ps(real, amplitude);
		imag = _mm256_mul_ps(imag, amplitude);

		//Math is done, now we need to shuffle them back
		//Shuffle 128-bit values to get rrrriiii rrrriiii.
		block0 = _mm256_permute2x128_si256(_mm256_castps_si256(real), _mm256_castps_si256(imag), 0x20);
		block1 = _mm256_permute2x128_si256(_mm256_castps_si256(real), _mm256_castps_si256(imag), 0x31);

		//Shuffle 64-bit values to get rriirrii
		block0 = _mm256_permute4x64_epi64(block0, 0xd8);
		block1 = _mm256_permute4x64_epi64(block1, 0xd8);

		//Shuffle 32-bit values to get the final value ready for writeback
		din0 =_mm256_permute_ps(_mm256_castsi256_ps(block0), 0xd8);
		din1 =_mm256_permute_ps(_mm256_castsi256_ps(block1), 0xd8);

		//Write back output
		_mm256_store_ps(&m_forwardOutBuf[i*2], din0);
		_mm256_store_ps(&m_forwardOutBuf[i*2] + 8, din1);
	}

	//Do any leftovers
	for(size_t i=end; i<nouts; i++)
	{
		float amplitude = m_resampledSparamAmplitudes[i];
		float cosval = m_resampledSparamCosines[i];
		float sinval = m_resampledSparamSines[i];

		//Uncorrected complex value
		float real_orig = m_forwardOutBuf[i*2 + 0];
		float imag_orig = m_forwardOutBuf[i*2 + 1];

		//Phase correction
		float real = real_orig*cosval - imag_orig*sinval;
		float imag = real_orig*sinval + imag_orig*cosval;

		//Amplitude correction
		m_forwardOutBuf[i*2 + 0] = real * amplitude;
		m_forwardOutBuf[i*2 + 1] = imag * amplitude;
	}
}
