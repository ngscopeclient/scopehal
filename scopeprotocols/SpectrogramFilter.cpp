/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2023 Andrew D. Zonenberg and contributors                                                         *
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

#ifndef _APPLE_SILICON

#include "../scopehal/scopehal.h"
#include "../scopehal/AlignedAllocator.h"
#include "SpectrogramFilter.h"
#ifdef __x86_64__
#include <immintrin.h>
#include "../scopehal/avx_mathfun.h"
#endif
#include "FFTFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SpectrogramWaveform::SpectrogramWaveform(size_t width, size_t height, double binsize)
	: DensityFunctionWaveform(width, height)
	, m_binsize(binsize)
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
{
	AddStream(Unit(Unit::UNIT_HZ), "data", Stream::STREAM_TYPE_SPECTROGRAM);

	//Set up channels
	CreateInput("din");

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

void SpectrogramFilter::ReallocateBuffers(size_t fftlen)
{
	m_cachedFFTLength = fftlen;

	size_t nouts = fftlen/2 + 1;
	if(m_vkPlan)
	{
		if(m_vkPlan->size() != fftlen)
			m_vkPlan = nullptr;
	}
	if(!m_vkPlan)
		m_vkPlan = make_unique<VulkanFFTPlan>(fftlen, nouts, VulkanFFTPlan::DIRECTION_FORWARD);

	//TODO: tweak
	m_rdinbuf.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
	m_rdinbuf.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
	//m_rdinbuf.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_NEVER);
	m_rdoutbuf.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
	//m_rdoutbuf.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_NEVER);
	m_rdoutbuf.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
}

void SpectrogramFilter::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	double start = GetTime();

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
	if(fftlen != m_cachedFFTLength)
		ReallocateBuffers(fftlen);
	size_t nblocks = inlen / fftlen;

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
	size_t nouts = fftlen/2 + 1;
	auto cap = new SpectrogramWaveform(
		nblocks,
		nouts,
		bin_hz);
	cap->PrepareForCpuAccess();
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	cap->m_triggerPhase = din->m_triggerPhase;
	cap->m_timescale = fs_per_sample * fftlen;
	SetData(cap, 0);

	//Make sure our temporary buffers are big enough
	m_rdinbuf.resize(fftlen * nblocks);
	m_rdoutbuf.resize(fftlen * nouts);

	//Cache a bunch of configuration
	auto window = static_cast<FFTFilter::WindowFunction>(m_parameters[m_windowName].GetIntVal());
	auto data = cap->GetData();
	float minscale = m_parameters[m_rangeMinName].GetFloatVal();
	float fullscale = m_parameters[m_rangeMaxName].GetFloatVal();
	float range = fullscale - minscale;

	//Run the FFTs
	for(size_t block=0; block<nblocks; block++)
	{
		//Grab the input and apply the window function
		//TODO gpu ify
		FFTFilter::ApplyWindow((float*)&din->m_samples[block*fftlen], fftlen, &m_rdinbuf[0], window);
		m_rdinbuf.MarkModifiedFromCpu();


		//Prepare to do all of our compute stuff in one dispatch call to reduce overhead
		cmdBuf.reset();
		cmdBuf.begin({});

		m_rdinbuf.PrepareForGpuAccessNonblocking(false, cmdBuf);

		//Input was modified from CPU
		m_vkPlan->AppendForward(
			m_rdinbuf,
			m_rdoutbuf,
			cmdBuf,
			/*block*fftlen*sizeof(float)*/0,
			/*block*fftlen*sizeof(float)*2*/0);

		//Done, block until the compute operations finish
		cmdBuf.end();
		queue->SubmitAndBlock(cmdBuf);

		m_rdoutbuf.MarkModifiedFromGpu();
		m_rdoutbuf.PrepareForCpuAccess();
		ProcessSpectrumGeneric(nblocks, block, nouts, minscale, range, scale, data);
	}

	//Temp: hack the output on the CPU
	/*m_rdoutbuf.MarkModifiedFromGpu();
	m_rdoutbuf.PrepareForCpuAccess();
	for(size_t block=0; block<nblocks; block++)
		ProcessSpectrumGeneric(nblocks, block, nouts, minscale, range, scale, data);*/

	cap->MarkModifiedFromCpu();

	double dt = GetTime() - start;
	LogDebug("SpectrogramFilter: %.3f ms\n", dt*1e3);
}

void SpectrogramFilter::ProcessSpectrumGeneric(
	size_t nblocks,
	size_t block,
	size_t nouts,
	float minscale,
	float range,
	float scale,
	float* data)
{
	const float impedance = 50;
	const float inverse_impedance = 1.0f / impedance;
	const float flog10 = log(10);
	const float logscale = 10 / flog10;
	const float irange = 1.0 / range;
	const float impscale = scale*scale * inverse_impedance;

	for(size_t i=0; i<nouts; i++)
	{
		float real = m_rdoutbuf[i*2 + 0];
		float imag = m_rdoutbuf[i*2 + 1];
		float vsq = real*real + imag*imag;
		float dbm = (logscale * log(vsq * impscale) + 30);
		if(dbm < minscale)
			data[i*nblocks + block] = 0;
		else
			data[i*nblocks + block] = (dbm - minscale) * irange;
	}
}
#endif
