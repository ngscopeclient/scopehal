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
#include "CouplerDeEmbedFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

CouplerDeEmbedFilter::CouplerDeEmbedFilter(const string& color)
	: Filter(color, CAT_RF)
	, m_maxGainName("Max Gain")
	, m_rectangularComputePipeline("shaders/RectangularWindow.spv", 2, sizeof(WindowFunctionArgs))
	, m_deEmbedComputePipeline("shaders/DeEmbedOutOfPlace.spv", 4, sizeof(uint32_t))
	, m_deEmbedInPlaceComputePipeline("shaders/DeEmbedFilter.spv", 3, sizeof(uint32_t))
	, m_normalizeComputePipeline("shaders/DeEmbedNormalization.spv", 2, sizeof(DeEmbedNormalizationArgs))
	, m_subtractInPlaceComputePipeline("shaders/SubtractInPlace.spv", 2, sizeof(uint32_t))
	, m_subtractComputePipeline("shaders/SubtractOutOfPlace.spv", 3, sizeof(uint32_t))
{
	AddStream(Unit(Unit::UNIT_VOLTS), "forward", Stream::STREAM_TYPE_ANALOG);
	AddStream(Unit(Unit::UNIT_VOLTS), "reverse", Stream::STREAM_TYPE_ANALOG);

	CreateInput("forward");
	CreateInput("reverse");
	CreateInput("forwardCoupMag");
	CreateInput("forwardCoupAng");
	CreateInput("reverseCoupMag");
	CreateInput("reverseCoupAng");

	CreateInput("forwardLeakMag");
	CreateInput("forwardLeakAng");
	CreateInput("reverseLeakMag");
	CreateInput("reverseLeakAng");

	m_parameters[m_maxGainName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_DB));
	m_parameters[m_maxGainName].SetFloatVal(30);

	m_cachedNumPoints = 0;
	m_cachedMaxGain = 0;

	m_scalarTempBuf1.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_NEVER);
	m_scalarTempBuf1.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);

	m_vectorTempBuf1.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_NEVER);
	m_vectorTempBuf1.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);

	m_vectorTempBuf2.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_NEVER);
	m_vectorTempBuf2.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);

	m_vectorTempBuf3.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_NEVER);
	m_vectorTempBuf3.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);

	m_vectorTempBuf4.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_NEVER);
	m_vectorTempBuf4.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
}

CouplerDeEmbedFilter::~CouplerDeEmbedFilter()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool CouplerDeEmbedFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	switch(i)
	{
		//forward and reverse path signals
		case 0:
		case 1:
			return (stream.GetType() == Stream::STREAM_TYPE_ANALOG);

		//mag
		case 2:
		case 4:
		case 6:
		case 8:
			return (stream.GetType() == Stream::STREAM_TYPE_ANALOG) &&
					(stream.GetYAxisUnits() == Unit::UNIT_DB);

		//angle
		case 3:
		case 5:
		case 7:
		case 9:
			return (stream.GetType() == Stream::STREAM_TYPE_ANALOG) &&
					(stream.GetYAxisUnits() == Unit::UNIT_DEGREES);

		default:
			return false;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string CouplerDeEmbedFilter::GetProtocolName()
{
	return "Coupler De-Embed";
}

Filter::DataLocation CouplerDeEmbedFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void CouplerDeEmbedFilter::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	//TODO: implement fallback
	if(!g_hasPushDescriptor)
	{
		LogError("CouplerDeEmbedFilter requires VK_KHR_push_descriptor\n");
		SetData(nullptr, 0);
		return;
	}

	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(nullptr, 0);
		return;
	}

	//Extract forward and reverse port waveforms
	auto dinFwd = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(0));
	auto dinRev = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(1));
	if(!dinFwd || !dinRev)
	{
		SetData(nullptr, 0);
		return;
	}
	const size_t npoints_raw = min(dinFwd->size(), dinRev->size());

	//Zero pad to next power of two up
	const size_t npoints = next_pow2(npoints_raw);
	//LogTrace("CouplerDeEmbedFilter: processing %zu raw points\n", npoints_raw);
	//LogTrace("Rounded to %zu\n", npoints);

	//Format the input data as raw samples for the FFT
	size_t nouts = npoints/2 + 1;

	//Invalidate old vkFFT plans if size has changed
	if(m_vkForwardPlan)
	{
		if(m_vkForwardPlan->size() != npoints)
			m_vkForwardPlan = nullptr;
	}
	if(m_vkForwardPlan2)
	{
		if(m_vkForwardPlan2->size() != npoints)
			m_vkForwardPlan = nullptr;
	}
	if(m_vkReversePlan)
	{
		if(m_vkReversePlan->size() != npoints)
			m_vkReversePlan = nullptr;
	}
	if(m_vkReversePlan2)
	{
		if(m_vkReversePlan2->size() != npoints)
			m_vkReversePlan2 = nullptr;
	}

	//Set up the FFT and allocate buffers if we change point count
	bool sizechange = false;
	if(m_cachedNumPoints != npoints)
	{
		m_scalarTempBuf1.resize(npoints);
		m_vectorTempBuf1.resize(2 * nouts);
		m_vectorTempBuf2.resize(2 * nouts);
		m_vectorTempBuf3.resize(2 * nouts);
		m_vectorTempBuf4.resize(2 * nouts);

		m_cachedNumPoints = npoints;
		sizechange = true;
	}

	//Set up new FFT plans
	if(!m_vkForwardPlan)
		m_vkForwardPlan = make_unique<VulkanFFTPlan>(npoints, nouts, VulkanFFTPlan::DIRECTION_FORWARD);
	if(!m_vkForwardPlan2)
		m_vkForwardPlan2 = make_unique<VulkanFFTPlan>(npoints, nouts, VulkanFFTPlan::DIRECTION_FORWARD);
	if(!m_vkReversePlan)
		m_vkReversePlan = make_unique<VulkanFFTPlan>(npoints, nouts, VulkanFFTPlan::DIRECTION_REVERSE);
	if(!m_vkReversePlan2)
		m_vkReversePlan2 = make_unique<VulkanFFTPlan>(npoints, nouts, VulkanFFTPlan::DIRECTION_REVERSE);

	//Calculate size of each bin
	double fs = dinFwd->m_timescale;
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

	float maxGain = pow(10, m_parameters[m_maxGainName].GetFloatVal()/20);

	//Resample S-parameters to our FFT bin size and cache where possible

	//Coupled paths
	auto dmag = GetInput(2).GetData();
	auto dang = GetInput(3).GetData();
	if(sizechange || clipchange || m_forwardCoupledParams.NeedUpdate(dmag, dang, bin_hz) )
		m_forwardCoupledParams.Refresh(dmag, dang, bin_hz, true, nouts, maxGain, dinFwd->m_timescale, npoints);

	dmag = GetInput(4).GetData();
	dang = GetInput(5).GetData();
	if(sizechange || clipchange || m_reverseCoupledParams.NeedUpdate(dmag, dang, bin_hz))
		m_reverseCoupledParams.Refresh(dmag, dang, bin_hz, true, nouts, maxGain, dinFwd->m_timescale, npoints);

	//Leakage paths
	dmag = GetInput(6).GetData();
	dang = GetInput(7).GetData();
	if(sizechange || clipchange || m_forwardLeakageParams.NeedUpdate(dmag, dang, bin_hz))
		m_forwardLeakageParams.Refresh(dmag, dang, bin_hz, false, nouts, maxGain, dinFwd->m_timescale, npoints);

	dmag = GetInput(8).GetData();
	dang = GetInput(9).GetData();
	if(sizechange || clipchange || m_reverseLeakageParams.NeedUpdate(dmag, dang, bin_hz))
		m_reverseLeakageParams.Refresh(dmag, dang, bin_hz, false, nouts, maxGain, dinFwd->m_timescale, npoints);

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	//Prepare to do all of our compute stuff in one dispatch call to reduce overhead
	cmdBuf.begin({});

	//Pad and FFT both inputs
	//vec1 = raw rev, vec3 = raw fwd
	ProcessScalarInput(cmdBuf, m_vkForwardPlan, dinFwd->m_samples, m_vectorTempBuf3, npoints, npoints_raw);
	ProcessScalarInput(cmdBuf, m_vkForwardPlan2, dinRev->m_samples, m_vectorTempBuf1, npoints, npoints_raw);

	//De-embed the forward path
	//vec1 = raw rev, vec2 = de-embedded fwd, vec3 = raw fwd
	ApplySParameters(cmdBuf, m_vectorTempBuf3, m_vectorTempBuf2, m_forwardCoupledParams, npoints, nouts);

	//Calculate forward path leakage from this
	//TODO: calculate and correct for group delay in the leakage path
	//vec1 = raw rev, vec2 = fwd leakage, vec3 = raw fwd
	ApplySParametersInPlace(cmdBuf, m_vectorTempBuf2, m_forwardLeakageParams, npoints, nouts);

	//Calculate reverse path signal minus leakage from the forward path
	//vec1 = raw reverse, vec2 = fwd leakage, vec3 = raw fwd, vec4 = clean reverse
	Subtract(cmdBuf, m_vectorTempBuf1, m_vectorTempBuf2, m_vectorTempBuf4, nouts*2);

	//Given signal minus leakage (enhanced isolation at the coupler output), de-embed coupler response
	//to get signal at coupler input
	//vec1 = raw reverse, vec2 = fwd leakage, vec3 = raw fwd, vec4 = clean reverse
	ApplySParametersInPlace(cmdBuf, m_vectorTempBuf4, m_reverseCoupledParams, npoints, nouts);

	//Generate final clean reverse path output
	size_t istart = 0;
	size_t iend = npoints_raw;
	int64_t phaseshift = 0;
	GroupDelayCorrection(m_reverseCoupledParams, istart, iend, phaseshift, true);
	GenerateScalarOutput(cmdBuf, m_vkReversePlan2, istart, iend, dinRev, 1, npoints, phaseshift, m_vectorTempBuf4);

	//De-embed the reverse path
	//vec1 = raw rev, vec2 = de-embedded reverse, vec3 = raw fwd
	ApplySParameters(cmdBuf, m_vectorTempBuf1, m_vectorTempBuf2, m_reverseCoupledParams, npoints, nouts);

	//Calculate reverse path leakage
	//TODO: calculate and correct for group delay in the leakage path
	//vec1 = raw rev, vec2 = reverse leakage, vec3 = raw fwd
	ApplySParametersInPlace(cmdBuf, m_vectorTempBuf2, m_reverseLeakageParams, npoints, nouts);

	//Calculate forward path signal minus leakage from the reverse path
	//vec1 = raw rev, vec2 = reverse leakage, vec3 = clean forward
	SubtractInPlace(cmdBuf, m_vectorTempBuf3, m_vectorTempBuf2, nouts*2);

	//Given signal minus leakage (enhanced isolation at the coupler output), de-embed coupler response
	//to get signal at coupler input
	//vec1 = raw rev, vec2 = reverse leakage, vec3 = final forward output
	ApplySParametersInPlace(cmdBuf, m_vectorTempBuf3, m_forwardCoupledParams, npoints, nouts);

	//Generate final clean forward path output
	istart = 0;
	iend = npoints_raw;
	GroupDelayCorrection(m_forwardCoupledParams, istart, iend, phaseshift, true);
	GenerateScalarOutput(cmdBuf, m_vkReversePlan, istart, iend, dinFwd, 0, npoints, phaseshift, m_vectorTempBuf3);

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	//Done, block until the compute operations finish
	cmdBuf.end();
	queue->SubmitAndBlock(cmdBuf);
}

/**
	@brief Subtract one signal from another and overwrite the first
 */
void CouplerDeEmbedFilter::SubtractInPlace(
		vk::raii::CommandBuffer& cmdBuf,
		AcceleratorBuffer<float>& samplesInout,
		AcceleratorBuffer<float>& samplesSub,
		size_t npoints)
{
	m_subtractInPlaceComputePipeline.Bind(cmdBuf);
	m_subtractInPlaceComputePipeline.BindBufferNonblocking(0, samplesInout, cmdBuf);
	m_subtractInPlaceComputePipeline.BindBufferNonblocking(1, samplesSub, cmdBuf);
	m_subtractInPlaceComputePipeline.DispatchNoRebind(cmdBuf, (uint32_t)npoints, GetComputeBlockCount(npoints, 64));
	m_subtractInPlaceComputePipeline.AddComputeMemoryBarrier(cmdBuf);
	samplesInout.MarkModifiedFromGpu();
}

/**
	@brief Subtract one signal from another and overwrite the first
 */
void CouplerDeEmbedFilter::Subtract(
		vk::raii::CommandBuffer& cmdBuf,
		AcceleratorBuffer<float>& samplesP,
		AcceleratorBuffer<float>& samplesN,
		AcceleratorBuffer<float>& samplesOut,
		size_t npoints)
{
	m_subtractComputePipeline.Bind(cmdBuf);
	m_subtractComputePipeline.BindBufferNonblocking(0, samplesP, cmdBuf);
	m_subtractComputePipeline.BindBufferNonblocking(1, samplesN, cmdBuf);
	m_subtractComputePipeline.BindBufferNonblocking(2, samplesOut, cmdBuf, true);
	m_subtractComputePipeline.DispatchNoRebind(cmdBuf, (uint32_t)npoints, GetComputeBlockCount(npoints, 64));
	m_subtractComputePipeline.AddComputeMemoryBarrier(cmdBuf);
	samplesOut.MarkModifiedFromGpu();
}

/**
	@brief Calculate bounds for the *meaningful* output data.
	Since we're phase shifting, there's gonna be some garbage response at one end of the channel.
 */
void CouplerDeEmbedFilter::GroupDelayCorrection(
	CouplerSParameters& params,
	size_t& istart,
	size_t& iend,
	int64_t& phaseshift,
	bool invert)
{
	if(invert)
	{
		iend -= params.m_groupDelaySamples;
		phaseshift = -params.m_groupDelayFs;
	}
	else
	{
		istart += params.m_groupDelaySamples;
		phaseshift = params.m_groupDelayFs;
	}
}

/**
	@brief Generates a scalar output from a complex input

	Overwrites m_scalarTempBuf1
 */
void CouplerDeEmbedFilter::GenerateScalarOutput(
	vk::raii::CommandBuffer& cmdBuf,
	unique_ptr<VulkanFFTPlan>& plan,
	size_t istart,
	size_t iend,
	WaveformBase* refin,
	size_t stream,
	size_t npoints,
	int64_t phaseshift,
	AcceleratorBuffer<float>& samplesIn)
{
	//Prepare the output waveform
	float scale = 1.0f / npoints;
	size_t outlen = iend - istart;
	auto cap = SetupEmptyUniformAnalogOutputWaveform(refin, stream);
	cap->Resize(outlen);

	//Apply phase shift for the group delay so we draw the waveform in the right place
	cap->m_triggerPhase = phaseshift;

	//Do the actual FFT operation
	plan->AppendReverse(samplesIn, m_scalarTempBuf1, cmdBuf);

	//Copy and normalize output
	//TODO: is there any way to fold this into vkFFT? They can normalize, but offset might be tricky...
	DeEmbedNormalizationArgs nargs;
	nargs.outlen = outlen;
	nargs.istart = istart;
	nargs.scale = scale;
	m_normalizeComputePipeline.Bind(cmdBuf);
	m_normalizeComputePipeline.BindBufferNonblocking(0, m_scalarTempBuf1, cmdBuf);
	m_normalizeComputePipeline.BindBufferNonblocking(1, cap->m_samples, cmdBuf, true);
	m_normalizeComputePipeline.DispatchNoRebind(cmdBuf, nargs, GetComputeBlockCount(npoints, 64));
	m_normalizeComputePipeline.AddComputeMemoryBarrier(cmdBuf);

	cap->MarkModifiedFromGpu();
}

/**
	@brief Apply a set of processed S-parameters (either forward or inverse channel response)
 */
void CouplerDeEmbedFilter::ApplySParameters(
		vk::raii::CommandBuffer& cmdBuf,
		AcceleratorBuffer<float>& samplesIn,
		AcceleratorBuffer<float>& samplesOut,
		CouplerSParameters& params,
		size_t npoints,
		size_t nouts)
{
	m_deEmbedComputePipeline.Bind(cmdBuf);
	m_deEmbedComputePipeline.BindBufferNonblocking(0, samplesIn, cmdBuf);
	m_deEmbedComputePipeline.BindBufferNonblocking(1, samplesOut, cmdBuf, true);
	m_deEmbedComputePipeline.BindBufferNonblocking(2, params.m_resampledSparamSines, cmdBuf);
	m_deEmbedComputePipeline.BindBufferNonblocking(3, params.m_resampledSparamCosines, cmdBuf);
	m_deEmbedComputePipeline.DispatchNoRebind(cmdBuf, (uint32_t)nouts, GetComputeBlockCount(npoints, 64));
	m_deEmbedComputePipeline.AddComputeMemoryBarrier(cmdBuf);
	samplesOut.MarkModifiedFromGpu();
}

/**
	@brief Apply a set of processed S-parameters (either forward or inverse channel response)
 */
void CouplerDeEmbedFilter::ApplySParametersInPlace(
		vk::raii::CommandBuffer& cmdBuf,
		AcceleratorBuffer<float>& samplesInout,
		CouplerSParameters& params,
		size_t npoints,
		size_t nouts)
{
	m_deEmbedInPlaceComputePipeline.Bind(cmdBuf);
	m_deEmbedInPlaceComputePipeline.BindBufferNonblocking(0, samplesInout, cmdBuf);
	m_deEmbedInPlaceComputePipeline.BindBufferNonblocking(1, params.m_resampledSparamSines, cmdBuf);
	m_deEmbedInPlaceComputePipeline.BindBufferNonblocking(2, params.m_resampledSparamCosines, cmdBuf);
	m_deEmbedInPlaceComputePipeline.DispatchNoRebind(cmdBuf, (uint32_t)nouts, GetComputeBlockCount(npoints, 64));
	m_deEmbedInPlaceComputePipeline.AddComputeMemoryBarrier(cmdBuf);
	samplesInout.MarkModifiedFromGpu();
}

/**
	@brief Zero-pad a scalar input to the proper length and FFT it

	Overwrites m_scalarTempBuf1
 */
void CouplerDeEmbedFilter::ProcessScalarInput(
	vk::raii::CommandBuffer& cmdBuf,
	unique_ptr<VulkanFFTPlan>& plan,
	AcceleratorBuffer<float>& samplesIn,
	AcceleratorBuffer<float>& samplesOut,
	size_t npointsPadded,
	size_t npointsUnpadded
	)
{
	//Copy and zero-pad the input as needed
	WindowFunctionArgs args;
	args.numActualSamples = npointsUnpadded;
	args.npoints = npointsPadded;
	args.scale = 0;
	args.alpha0 = 0;
	args.alpha1 = 0;
	args.offsetIn = 0;
	args.offsetOut = 0;
	m_rectangularComputePipeline.Bind(cmdBuf);
	m_rectangularComputePipeline.BindBufferNonblocking(0, samplesIn, cmdBuf);
	m_rectangularComputePipeline.BindBufferNonblocking(1, m_scalarTempBuf1, cmdBuf, true);
	m_rectangularComputePipeline.DispatchNoRebind(cmdBuf, args, GetComputeBlockCount(npointsPadded, 64));
	m_rectangularComputePipeline.AddComputeMemoryBarrier(cmdBuf);
	m_scalarTempBuf1.MarkModifiedFromGpu();

	//Do the actual FFT operation
	plan->AppendForward(m_scalarTempBuf1, samplesOut, cmdBuf);
	samplesOut.MarkModifiedFromGpu();
	m_rectangularComputePipeline.AddComputeMemoryBarrier(cmdBuf);
}

/**
	@brief Returns the max mid-band group delay of the channel
 */
int64_t CouplerSParameters::GetGroupDelay()
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
 */
void CouplerSParameters::InterpolateSparameters(
	WaveformBase* wmag,
	WaveformBase* wang,
	float bin_hz,
	bool invert,
	size_t nouts,
	float maxGain)
{
	m_cachedBinSize = bin_hz;

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
