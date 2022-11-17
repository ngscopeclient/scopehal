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
#include "DeEmbedFilter.h"
#ifdef __x86_64__
#include <immintrin.h>
#endif

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DeEmbedFilter::DeEmbedFilter(const string& color)
	: Filter(color, CAT_ANALYSIS)
	, m_rectangularComputePipeline("shaders/RectangularWindow.spv", 2, sizeof(WindowFunctionArgs))
	, m_deEmbedComputePipeline("shaders/DeEmbedFilter.spv", 3, sizeof(uint32_t))
	, m_normalizeComputePipeline("shaders/DeEmbedNormalization.spv", 2, sizeof(DeEmbedNormalizationArgs))
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput("signal");
	CreateInput("mag");
	CreateInput("angle");

	m_maxGainName = "Max Gain";
	m_parameters[m_maxGainName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_DB));
	m_parameters[m_maxGainName].SetFloatVal(20);

	m_groupDelayTruncName = "Group Delay Truncation";
	m_parameters[m_groupDelayTruncName] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));
	m_parameters[m_groupDelayTruncName].SetFloatVal(0);

	m_groupDelayTruncModeName = "Group Delay Truncation Mode";
	m_parameters[m_groupDelayTruncModeName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_groupDelayTruncModeName].AddEnumValue("Auto", TRUNC_AUTO);
	m_parameters[m_groupDelayTruncModeName].AddEnumValue("Manual", TRUNC_MANUAL);
	m_parameters[m_groupDelayTruncModeName].SetIntVal(TRUNC_AUTO);

	m_cachedBinSize = 0;

#ifndef _APPLE_SILICON
	m_forwardPlan = NULL;
	m_reversePlan = NULL;
#endif

	m_cachedNumPoints = 0;
	m_cachedMaxGain = 0;

	m_forwardInBuf.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
	m_forwardInBuf.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);

	m_forwardOutBuf.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
	m_forwardOutBuf.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);

	m_reverseOutBuf.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
	m_reverseOutBuf.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);

}

DeEmbedFilter::~DeEmbedFilter()
{
#ifndef _APPLE_SILICON
	if(m_forwardPlan)
		ffts_free(m_forwardPlan);
	if(m_reversePlan)
		ffts_free(m_reversePlan);

	m_forwardPlan = NULL;
	m_reversePlan = NULL;
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DeEmbedFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	switch(i)
	{
		//signal
		case 0:
			return (stream.GetType() == Stream::STREAM_TYPE_ANALOG);

		//mag
		case 1:
			return (stream.GetType() == Stream::STREAM_TYPE_ANALOG) &&
					(stream.GetYAxisUnits() == Unit::UNIT_DB);

		//angle
		case 2:
			return (stream.GetType() == Stream::STREAM_TYPE_ANALOG) &&
					(stream.GetYAxisUnits() == Unit::UNIT_DEGREES);

		default:
			return false;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string DeEmbedFilter::GetProtocolName()
{
	return "De-Embed";
}

Filter::DataLocation DeEmbedFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DeEmbedFilter::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	DoRefresh(true, cmdBuf, queue);
}

/**
	@brief Applies the S-parameters in the forward or reverse direction
 */
void DeEmbedFilter::DoRefresh(bool invert, vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	auto din = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(0));
	if(!din)
	{
		SetData(NULL, 0);
		return;
	}
	const size_t npoints_raw = din->size();

	//Zero pad to next power of two up
	const size_t npoints = next_pow2(npoints_raw);
	//LogTrace("DeEmbedFilter: processing %zu raw points\n", npoints_raw);
	//LogTrace("Rounded to %zu\n", npoints);

	//Format the input data as raw samples for the FFT
	size_t nouts = npoints/2 + 1;

	//Invalidate old vkFFT plans if size has changed
	if(m_vkForwardPlan)
	{
		if(m_vkForwardPlan->size() != npoints)
			m_vkForwardPlan = nullptr;
	}
	if(m_vkReversePlan)
	{
		if(m_vkReversePlan->size() != npoints)
			m_vkReversePlan = nullptr;
	}

	//Set up the FFT and allocate buffers if we change point count
	bool sizechange = false;
	if(m_cachedNumPoints != npoints)
	{
	#ifndef _APPLE_SILICON
		//Delete old FFTS plan objects
		if(m_forwardPlan)
		{
			ffts_free(m_forwardPlan);
			m_forwardPlan = nullptr;
		}
		if(m_reversePlan)
		{
			ffts_free(m_reversePlan);
			m_reversePlan = nullptr;
		}
	#endif

		m_forwardInBuf.resize(npoints);
		m_forwardOutBuf.resize(2 * nouts);
		m_reverseOutBuf.resize(npoints);

		m_cachedNumPoints = npoints;
		sizechange = true;

		//Make new plans
		//(save time, don't make FFTS plans if using vkFFT)
		if(!g_gpuFilterEnabled)
		{
		#ifdef _APPLE_SILICON
			LogFatal("DeEmbedFilter CPU fallback not available on Apple Silicon");
		#else
			m_forwardPlan = ffts_init_1d_real(npoints, FFTS_FORWARD);
			m_reversePlan = ffts_init_1d_real(npoints, FFTS_BACKWARD);
		#endif
		}
	}

	//Set up new FFT plans
	if(g_gpuFilterEnabled)
	{
		if(!m_vkForwardPlan)
			m_vkForwardPlan = make_unique<VulkanFFTPlan>(npoints, nouts, VulkanFFTPlan::DIRECTION_FORWARD);
		if(!m_vkReversePlan)
			m_vkReversePlan = make_unique<VulkanFFTPlan>(npoints, nouts, VulkanFFTPlan::DIRECTION_REVERSE);
	}

	//Calculate size of each bin
	double fs = din->m_timescale;
	double sample_ghz = 1e6 / fs;
	double bin_hz = round((0.5f * sample_ghz * 1e9f) / nouts);

	//Did we change the max gain?
	bool clipchange = false;
	float maxgain = m_parameters[m_maxGainName].GetFloatVal();
	if(maxgain != m_cachedMaxGain)
	{
		m_cachedMaxGain = maxgain;
		clipchange = true;
		ClearSweeps();
	}

	//Waveform object changed? Input parameters are no longer valid
	//We need check for input count because CTLE filter generates S-params internally (and deletes the mag/angle inputs)
	//TODO: would it be cleaner to generate filter response then channel-emulate it?
	bool inchange = false;
	if(GetInputCount() > 1)
	{
		auto dmag = GetInput(1).GetData();
		auto dang = GetInput(2).GetData();
		if( (m_magKey != dmag) ||
			(m_angleKey != dang) )
		{
			inchange = true;

			m_magKey = dmag;
			m_angleKey = dang;
		}
	}

	//Resample our parameter to our FFT bin size if needed.
	//Cache trig function output because there's no AVX instructions for this.
	if( (fabs(m_cachedBinSize - bin_hz) > FLT_EPSILON) || sizechange || clipchange || inchange)
	{
		m_resampledSparamCosines.clear();
		m_resampledSparamSines.clear();
		InterpolateSparameters(bin_hz, invert, nouts);
	}

	//Calculate maximum group delay for the first few S-parameter bins (approx propagation delay of the channel)
	int64_t groupdelay_fs = GetGroupDelay();
	if(m_parameters[m_groupDelayTruncModeName].GetIntVal() == TRUNC_MANUAL)
		groupdelay_fs = m_parameters[m_groupDelayTruncName].GetIntVal();

	int64_t groupdelay_samples = ceil( groupdelay_fs / din->m_timescale );

	//Sanity check: if we have noisy or poor quality S-parameter data, group delay might not make sense.
	//Skip this correction pass in that case.
	if(llabs(groupdelay_samples) >= npoints)
	{
		groupdelay_fs = 0;
		groupdelay_samples = 0;
	}

	//Calculate bounds for the *meaningful* output data.
	//Since we're phase shifting, there's gonna be some garbage response at one end of the channel.
	size_t istart = 0;
	size_t iend = npoints_raw;
	UniformAnalogWaveform* cap = NULL;
	if(invert)
		iend -= groupdelay_samples;
	else
		istart += groupdelay_samples;
	cap = SetupEmptyUniformAnalogOutputWaveform(din, 0);

	//Apply phase shift for the group delay so we draw the waveform in the right place
	if(invert)
		cap->m_triggerPhase = -groupdelay_fs;
	else
		cap->m_triggerPhase = groupdelay_fs;
	float scale = 1.0f / npoints;
	size_t outlen = iend - istart;
	cap->Resize(outlen);

	if(g_gpuFilterEnabled)
	{
		//Prepare to do all of our compute stuff in one dispatch call to reduce overhead
		cmdBuf.begin({});

		//Copy and zero-pad the input as needed
		WindowFunctionArgs args;
		args.numActualSamples = npoints_raw;
		args.npoints = npoints;
		args.scale = 0;
		args.alpha0 = 0;
		args.alpha1 = 0;
		m_rectangularComputePipeline.BindBufferNonblocking(0, din->m_samples, cmdBuf);
		m_rectangularComputePipeline.BindBufferNonblocking(1, m_forwardInBuf, cmdBuf, true);
		m_rectangularComputePipeline.Dispatch(cmdBuf, args, GetComputeBlockCount(npoints, 64));
		m_rectangularComputePipeline.AddComputeMemoryBarrier(cmdBuf);
		m_forwardInBuf.MarkModifiedFromGpu();

		//Do the actual FFT operation
		m_vkForwardPlan->AppendForward(m_forwardInBuf, m_forwardOutBuf, cmdBuf);

		//Apply the interpolated S-parameters
		m_deEmbedComputePipeline.BindBufferNonblocking(0, m_forwardOutBuf, cmdBuf);
		m_deEmbedComputePipeline.BindBufferNonblocking(1, m_resampledSparamSines, cmdBuf);
		m_deEmbedComputePipeline.BindBufferNonblocking(2, m_resampledSparamCosines, cmdBuf);
		m_deEmbedComputePipeline.Dispatch(cmdBuf, (uint32_t)nouts, GetComputeBlockCount(npoints, 64));
		m_deEmbedComputePipeline.AddComputeMemoryBarrier(cmdBuf);
		m_forwardOutBuf.MarkModifiedFromGpu();

		//Do the actual FFT operation
		m_vkReversePlan->AppendReverse(m_forwardOutBuf, m_reverseOutBuf, cmdBuf);

		//Copy and normalize output
		//TODO: is there any way to fold this into vkFFT? They can normalize, but offset might be tricky...
		DeEmbedNormalizationArgs nargs;
		nargs.outlen = outlen;
		nargs.istart = istart;
		nargs.scale = scale;
		m_normalizeComputePipeline.BindBufferNonblocking(0, m_reverseOutBuf, cmdBuf);
		m_normalizeComputePipeline.BindBufferNonblocking(1, cap->m_samples, cmdBuf, true);
		m_normalizeComputePipeline.Dispatch(cmdBuf, nargs, GetComputeBlockCount(npoints, 64));
		m_normalizeComputePipeline.AddComputeMemoryBarrier(cmdBuf);

		//Done, block until the compute operations finish
		cmdBuf.end();
		queue->SubmitAndBlock(cmdBuf);
		cap->MarkModifiedFromGpu();
	}
	else
	#ifdef _APPLE_SILICON
		LogFatal("DeEmbedFilter CPU fallback not available on Apple Silicon");
	#else
	{
		din->PrepareForCpuAccess();
		m_forwardInBuf.PrepareForCpuAccess();
		m_forwardOutBuf.PrepareForCpuAccess();
		m_reverseOutBuf.PrepareForCpuAccess();
		cap->PrepareForCpuAccess();

		//Copy the input, then fill any extra space with zeroes
		memcpy(m_forwardInBuf.GetCpuPointer(), din->m_samples.GetCpuPointer(), npoints_raw*sizeof(float));
		for(size_t i=npoints_raw; i<npoints; i++)
			m_forwardInBuf[i] = 0;

		//Do the forward FFT
		ffts_execute(m_forwardPlan, m_forwardInBuf.GetCpuPointer(), m_forwardOutBuf.GetCpuPointer());

		//Apply the interpolated S-parameters
		#ifdef __x86_64__
		if(g_hasAvx2)
			MainLoopAVX2(nouts);
		else
		#endif
			MainLoop(nouts);

		//Calculate the inverse FFT
		ffts_execute(m_reversePlan, &m_forwardOutBuf[0], &m_reverseOutBuf[0]);

		//Copy waveform data after rescaling
		for(size_t i=0; i<outlen; i++)
			cap->m_samples[i] = m_reverseOutBuf[i+istart] * scale;

		cap->MarkModifiedFromCpu();
	}
	#endif
}

/**
	@brief Returns the max mid-band group delay of the channel
 */
int64_t DeEmbedFilter::GetGroupDelay()
{
	float max_delay = 0;
	size_t mid = m_cachedSparams.size() / 2;
	for(size_t i=0; i<50; i++)
	{
		size_t n = i+mid;
		if(n >= m_cachedSparams.size())
			break;

		max_delay = max(max_delay, m_cachedSparams.GetGroupDelay(n));
	}
	return max_delay * FS_PER_SECOND;
}

/**
	@brief Recalculate the cached S-parameters (and clamp gain if requested)

	Since there's no AVX sin/cos instructions, precompute sin(phase) and cos(phase)
 */
void DeEmbedFilter::InterpolateSparameters(float bin_hz, bool invert, size_t nouts)
{
	m_cachedBinSize = bin_hz;

	float maxGain = pow(10, m_parameters[m_maxGainName].GetFloatVal()/20);

	//Extract the S-parameters
	auto wmag = GetInputWaveform(1);
	auto wang = GetInputWaveform(2);
	wmag->PrepareForCpuAccess();
	wang->PrepareForCpuAccess();

	m_resampledSparamSines.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
	m_resampledSparamSines.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);

	m_resampledSparamCosines.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
	m_resampledSparamCosines.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);

	auto smag = dynamic_cast<SparseAnalogWaveform*>(wmag);
	auto sang = dynamic_cast<SparseAnalogWaveform*>(wang);
	auto umag = dynamic_cast<UniformAnalogWaveform*>(wmag);
	auto uang = dynamic_cast<UniformAnalogWaveform*>(wang);

	if(smag && sang)
		m_cachedSparams.ConvertFromWaveforms(smag, sang);
	else
		m_cachedSparams.ConvertFromWaveforms(umag, uang);

	m_resampledSparamSines.resize(nouts);
	m_resampledSparamCosines.resize(nouts);

	//De-embedding
	if(invert)
	{
		for(size_t i=0; i<nouts; i++)
		{
			float freq = bin_hz * i;
			auto pt = m_cachedSparams.InterpolatePoint(freq);
			float mag = pt.m_amplitude;
			float ang = pt.m_phase;

			float amp = 0;
			if(fabs(mag) > FLT_EPSILON)
				amp = 1.0f / mag;
			amp = min(amp, maxGain);

			m_resampledSparamSines[i] = sin(-ang) * amp;
			m_resampledSparamCosines[i] = cos(-ang) * amp;
		}
	}

	//Channel emulation
	else
	{
		for(size_t i=0; i<nouts; i++)
		{
			float freq = bin_hz * i;
			auto pt = m_cachedSparams.InterpolatePoint(freq);
			float mag = pt.m_amplitude;
			float ang = pt.m_phase;

			m_resampledSparamSines[i] = sin(ang) * mag;
			m_resampledSparamCosines[i] = cos(ang) * mag;
		}
	}

	m_resampledSparamSines.MarkModifiedFromCpu();
	m_resampledSparamCosines.MarkModifiedFromCpu();
}

void DeEmbedFilter::MainLoop(size_t nouts)
{
	for(size_t i=0; i<nouts; i++)
	{
		float sinval = m_resampledSparamSines[i];
		float cosval = m_resampledSparamCosines[i];

		//Uncorrected complex value
		float real_orig = m_forwardOutBuf[i*2 + 0];
		float imag_orig = m_forwardOutBuf[i*2 + 1];

		//Amplitude correction
		m_forwardOutBuf[i*2 + 0] = real_orig*cosval - imag_orig*sinval;
		m_forwardOutBuf[i*2 + 1] = real_orig*sinval + imag_orig*cosval;
	}
}

#ifdef __x86_64__
__attribute__((target("avx2")))
void DeEmbedFilter::MainLoopAVX2(size_t nouts)
{
	unsigned int end = nouts - (nouts % 8);

	//Vectorized loop doing 8 elements at once
	for(size_t i=0; i<end; i += 8)
	{
		//Load S-parameters
		//Precomputed sin/cos vector scaled by amplitude already
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
		//Fetch inputs
		float cosval = m_resampledSparamCosines[i];
		float sinval = m_resampledSparamSines[i];
		float real_orig = m_forwardOutBuf[i*2 + 0];
		float imag_orig = m_forwardOutBuf[i*2 + 1];

		//Do the actual phase correction
		m_forwardOutBuf[i*2 + 0] = real_orig*cosval - imag_orig*sinval;
		m_forwardOutBuf[i*2 + 1] = real_orig*sinval + imag_orig*cosval;
	}
}
#endif /* __x86_64__ */
