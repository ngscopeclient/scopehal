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
#include "../scopehal/AlignedAllocator.h"
#include "FFTFilter.h"

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
	m_cachedNumOuts = 0;

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
}

FFTFilter::~FFTFilter()
{
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
		m_cachedNumPointsFFT = npoints;

	//Update our FFT plan if it's out of date
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

	m_rdinbuf.resize(npoints);
	m_rdoutbuf.resize(2*nouts);
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
	m_cachedNumOuts = nouts;
	if(m_cachedNumPoints != npoints_raw)
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

	//Configure the window
	WindowFunctionArgs args;
	args.numActualSamples = numActualSamples;
	args.npoints = npoints;
	args.scale = 2 * M_PI / numActualSamples;
	args.offsetIn = 0;
	args.offsetOut = 0;
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

	//Peak search (for now this runs on the CPU)
	FindPeaks(cap);
}
