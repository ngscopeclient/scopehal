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
#include "SpectrogramFilter.h"
#include "FFTFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SpectrogramWaveform::SpectrogramWaveform(size_t width, size_t height, double binsize, double bottomEdgeFrequency)
	: DensityFunctionWaveform(width, height)
	, m_binsize(binsize)
	, m_bottomEdgeFrequency(bottomEdgeFrequency)
{

}

SpectrogramWaveform::~SpectrogramWaveform()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SpectrogramFilter::SpectrogramFilter(const string& color)
	: Filter(color, CAT_RF)
	, m_windowName("Window")
	, m_fftLengthName("FFT length")
	, m_rangeMinName("Range Min")
	, m_rangeMaxName("Range Max")
	, m_blackmanHarrisComputePipeline("shaders/BlackmanHarrisWindow.spv", 2, sizeof(WindowFunctionArgs))
	, m_rectangularComputePipeline("shaders/RectangularWindow.spv", 2, sizeof(WindowFunctionArgs))
	, m_cosineSumComputePipeline("shaders/CosineSumWindow.spv", 2, sizeof(WindowFunctionArgs))
	, m_postprocessComputePipeline("shaders/SpectrogramPostprocess.spv", 2, sizeof(SpectrogramPostprocessArgs))
{
	AddStream(Unit(Unit::UNIT_HZ), "data", Stream::STREAM_TYPE_SPECTROGRAM);

	//Set up channels
	CreateInput("din");

	//Default config
	m_range = 1e9;
	m_offset = -5e8;
	m_cachedFFTLength = 0;
	m_cachedFFTNumBlocks = 0;

	m_parameters[m_windowName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_windowName].AddEnumValue("Blackman-Harris", FFTFilter::FFTFilter::WINDOW_BLACKMAN_HARRIS);
	m_parameters[m_windowName].AddEnumValue("Hamming", FFTFilter::FFTFilter::WINDOW_HAMMING);
	m_parameters[m_windowName].AddEnumValue("Hann", FFTFilter::FFTFilter::WINDOW_HANN);
	m_parameters[m_windowName].AddEnumValue("Rectangular", FFTFilter::FFTFilter::WINDOW_RECTANGULAR);
	m_parameters[m_windowName].SetIntVal(FFTFilter::FFTFilter::WINDOW_BLACKMAN_HARRIS);

	m_parameters[m_fftLengthName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_SAMPLEDEPTH));
	m_parameters[m_fftLengthName].AddEnumValue("64", 64);
	m_parameters[m_fftLengthName].AddEnumValue("128", 128);
	m_parameters[m_fftLengthName].AddEnumValue("256", 256);
	m_parameters[m_fftLengthName].AddEnumValue("512", 512);
	m_parameters[m_fftLengthName].AddEnumValue("1024", 1024);
	m_parameters[m_fftLengthName].AddEnumValue("2048", 2048);
	m_parameters[m_fftLengthName].AddEnumValue("4096", 4096);
	m_parameters[m_fftLengthName].AddEnumValue("8192", 8192);
	m_parameters[m_fftLengthName].AddEnumValue("16384", 16384);
	m_parameters[m_fftLengthName].AddEnumValue("32768", 32768);
	m_parameters[m_fftLengthName].SetIntVal(512);

	m_parameters[m_rangeMaxName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_DBM));
	m_parameters[m_rangeMaxName].SetFloatVal(-10);

	m_parameters[m_rangeMinName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_DBM));
	m_parameters[m_rangeMinName].SetFloatVal(-50);
}

SpectrogramFilter::~SpectrogramFilter()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool SpectrogramFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

float SpectrogramFilter::GetOffset(size_t /*stream*/)
{
	return m_offset;
}

float SpectrogramFilter::GetVoltageRange(size_t /*stream*/)
{
	return m_range;
}

void SpectrogramFilter::SetVoltageRange(float range, size_t /*stream*/)
{
	m_range = range;
}

void SpectrogramFilter::SetOffset(float offset, size_t /*stream*/)
{
	m_offset = offset;
}

string SpectrogramFilter::GetProtocolName()
{
	return "Spectrogram";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void SpectrogramFilter::ReallocateBuffers(size_t fftlen, size_t nblocks)
{
	m_cachedFFTLength = fftlen;
	m_cachedFFTNumBlocks = nblocks;

	size_t nouts = fftlen/2 + 1;
	if(m_vkPlan)
	{
		if(m_vkPlan->size() != fftlen)
			m_vkPlan = nullptr;
	}
	if(!m_vkPlan)
		m_vkPlan = make_unique<VulkanFFTPlan>(fftlen, nouts, VulkanFFTPlan::DIRECTION_FORWARD, nblocks);

	m_rdinbuf.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_NEVER);
	m_rdinbuf.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
	m_rdoutbuf.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_NEVER);
	m_rdoutbuf.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
}

FlowGraphNode::DataLocation SpectrogramFilter::GetInputLocation()
{
	return LOC_DONTCARE;
}

void SpectrogramFilter::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndUniformAnalog())
	{
		SetData(NULL, 0);
		return;
	}
	auto din = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(0));

	//Figure out how many FFTs to do
	//For now, consecutive blocks and not a sliding window
	size_t inlen = din->size();
	size_t fftlen = m_parameters[m_fftLengthName].GetIntVal();
	size_t nblocks = floor(inlen * 1.0 / fftlen);

	if( (fftlen != m_cachedFFTLength) || (nblocks != m_cachedFFTNumBlocks) )
		ReallocateBuffers(fftlen, nblocks);

	//Figure out range of the FFTs
	double fs_per_sample = din->m_timescale;
	float scale = 2.0 / fftlen;
	double sample_ghz = 1e6 / fs_per_sample;
	double bin_hz = round((sample_ghz * 1e9f) / fftlen);
	double fmax = bin_hz * fftlen;

	Unit hz(Unit::UNIT_HZ);
	LogTrace("SpectrogramFilter: %zu input points, %zu %zu-point FFTs\n", inlen, nblocks, fftlen);
	LogIndenter li;
	LogTrace("FFT range is DC to %s\n", hz.PrettyPrint(fmax).c_str());
	LogTrace("%s per bin\n", hz.PrettyPrint(bin_hz).c_str());

	//Create the output
	//Reuse existing buffer if available and same size
	size_t nouts = fftlen/2 + 1;
	SpectrogramWaveform* cap = dynamic_cast<SpectrogramWaveform*>(GetData(0));
	if(cap)
	{
		if( (cap->GetBinSize() == bin_hz) &&
			(cap->GetWidth() == nblocks) &&
			(cap->GetHeight() == nouts) )
		{
			//same config, we can reuse it
		}

		//no, ignore it
		else
			cap = nullptr;
	}
	if(!cap)
	{
		cap = new SpectrogramWaveform(
			nblocks,
			nouts,
			bin_hz,
			0
			);
	}
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	cap->m_triggerPhase = din->m_triggerPhase;
	cap->m_timescale = fs_per_sample * fftlen;
	cap->PrepareForGpuAccess();
	SetData(cap, 0);

	//We also need to adjust the scale by the coherent power gain of the window function
	auto window = static_cast<FFTFilter::WindowFunction>(m_parameters[m_windowName].GetIntVal());
	switch(window)
	{
		case FFTFilter::WINDOW_HAMMING:
			scale *= 1.862;
			break;

		case FFTFilter::WINDOW_HANN:
			scale *= 2.013;
			break;

		case FFTFilter::WINDOW_BLACKMAN_HARRIS:
			scale *= 2.805;
			break;

		//unit
		case FFTFilter::WINDOW_RECTANGULAR:
		default:
			break;
	}

	//Configure the window
	WindowFunctionArgs args;
	args.numActualSamples = fftlen;
	args.npoints = fftlen;
	args.scale = 2 * M_PI / fftlen;
	switch(window)
	{
		case FFTFilter::WINDOW_HANN:
			args.alpha0 = 0.5;
			break;

		case FFTFilter::WINDOW_HAMMING:
			args.alpha0 = 25.0f / 46;
			break;

		default:
			args.alpha0 = 0;
			break;
	}
	args.alpha1 = 1 - args.alpha0;

	//Figure out which window shader to use
	ComputePipeline* wpipe = nullptr;
	switch(window)
	{
		case FFTFilter::WINDOW_BLACKMAN_HARRIS:
			wpipe = &m_blackmanHarrisComputePipeline;
			break;

		case FFTFilter::WINDOW_HANN:
		case FFTFilter::WINDOW_HAMMING:
			wpipe = &m_cosineSumComputePipeline;
			break;

		default:
		case FFTFilter::WINDOW_RECTANGULAR:
			wpipe = &m_rectangularComputePipeline;
			break;
	}

	//Make sure our temporary buffers are big enough
	m_rdinbuf.resize(nblocks * fftlen);
	m_rdoutbuf.resize(nblocks * (nouts * 2) );

	//Cache a bunch of configuration
	float minscale = m_parameters[m_rangeMinName].GetFloatVal();
	float fullscale = m_parameters[m_rangeMaxName].GetFloatVal();
	float range = fullscale - minscale;

	//Prepare to do all of our compute stuff in one dispatch call to reduce overhead
	cmdBuf.begin({});

	//Grab the input and apply the window function
	wpipe->BindBufferNonblocking(0, din->m_samples, cmdBuf);
	wpipe->BindBufferNonblocking(1, m_rdinbuf, cmdBuf, true);
	for(size_t block=0; block<nblocks; block++)
	{
		args.offsetIn = block*fftlen;
		args.offsetOut = block*fftlen;

		if(block == 0)
			wpipe->Dispatch(cmdBuf, args, GetComputeBlockCount(fftlen, 64));
		else
			wpipe->DispatchNoRebind(cmdBuf, args, GetComputeBlockCount(fftlen, 64));
	}
	wpipe->AddComputeMemoryBarrier(cmdBuf);

	//Do the actual FFT
	m_vkPlan->AppendForward(
		m_rdinbuf,
		m_rdoutbuf,
		cmdBuf);

	//Postprocess the output
	const float impedance = 50;
	SpectrogramPostprocessArgs postargs;
	postargs.nblocks = nblocks;
	postargs.nouts = nouts;
	postargs.logscale = 10.0 / log(10);
	postargs.impscale = scale*scale / impedance;
	postargs.minscale = minscale;
	postargs.irange = 1.0 / range;
	postargs.ygrid = min(g_maxComputeGroupCount[2], nblocks);
	m_postprocessComputePipeline.AddComputeMemoryBarrier(cmdBuf);
	m_postprocessComputePipeline.BindBufferNonblocking(0, m_rdoutbuf, cmdBuf);
	m_postprocessComputePipeline.BindBufferNonblocking(1, cap->GetOutData(), cmdBuf, true);
	m_postprocessComputePipeline.Dispatch(
		cmdBuf,
		postargs,
		GetComputeBlockCount(nouts, 64),
		ceil(nblocks * 1.0 / postargs.ygrid),
		postargs.ygrid
		);

	//Done, block until the compute operations finish
	cmdBuf.end();
	queue->SubmitAndBlock(cmdBuf);

	cap->MarkModifiedFromGpu();
}
