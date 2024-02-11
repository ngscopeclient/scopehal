/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2024 Andrew D. Zonenberg and contributors                                                         *
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

	m_cachedNumPoints = 0;
	m_cachedMaxGain = 0;
	m_cachedOutLen = 0;
	m_cachedIstart = 0;

	m_forwardInBuf.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
	m_forwardInBuf.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);

	m_forwardOutBuf.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
	m_forwardOutBuf.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);

	m_reverseOutBuf.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
	m_reverseOutBuf.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);

}

DeEmbedFilter::~DeEmbedFilter()
{
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
		m_forwardInBuf.resize(npoints);
		m_forwardOutBuf.resize(2 * nouts);
		m_reverseOutBuf.resize(npoints);

		m_cachedNumPoints = npoints;
		sizechange = true;
	}

	//Set up new FFT plans
	if(!m_vkForwardPlan)
		m_vkForwardPlan = make_unique<VulkanFFTPlan>(npoints, nouts, VulkanFFTPlan::DIRECTION_FORWARD);
	if(!m_vkReversePlan)
		m_vkReversePlan = make_unique<VulkanFFTPlan>(npoints, nouts, VulkanFFTPlan::DIRECTION_REVERSE);

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
	if(static_cast<size_t>(llabs(groupdelay_samples)) >= npoints)
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
	m_cachedIstart = istart;
	cap = SetupEmptyUniformAnalogOutputWaveform(din, 0);

	//Apply phase shift for the group delay so we draw the waveform in the right place
	if(invert)
		cap->m_triggerPhase = -groupdelay_fs;
	else
		cap->m_triggerPhase = groupdelay_fs;
	float scale = 1.0f / npoints;
	size_t outlen = iend - istart;
	cap->Resize(outlen);
	m_cachedOutLen = outlen;

	//Prepare to do all of our compute stuff in one dispatch call to reduce overhead
	cmdBuf.begin({});

	//Copy and zero-pad the input as needed
	WindowFunctionArgs args;
	args.numActualSamples = npoints_raw;
	args.npoints = npoints;
	args.scale = 0;
	args.alpha0 = 0;
	args.alpha1 = 0;
	args.offsetIn = 0;
	args.offsetOut = 0;
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
