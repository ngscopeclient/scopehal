/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
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
#include "FIRFilter.h"
#ifdef __x86_64__
#include <immintrin.h>
#endif

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

FIRFilter::FIRFilter(const string& color)
	: Filter(color, CAT_MATH, Unit(Unit::UNIT_FS))
	, m_filterType(m_parameters["Filter Type"])
	, m_filterLength(m_parameters["Length"])
	, m_stopbandAtten(m_parameters["Stopband Attenuation"])
	, m_freqLow(m_parameters["Frequency Low"])
	, m_freqHigh(m_parameters["Frequency High"])
	, m_computePipeline("shaders/FIRFilter.spv", 3, sizeof(FIRFilterArgs))
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput<InputConstraintStreamType>("in", Stream::STREAM_TYPE_ANALOG);

	m_filterType = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_filterType.AddEnumValue("Low pass", FILTER_TYPE_LOWPASS);
	m_filterType.AddEnumValue("High pass", FILTER_TYPE_HIGHPASS);
	m_filterType.AddEnumValue("Band pass", FILTER_TYPE_BANDPASS);
	m_filterType.AddEnumValue("Notch", FILTER_TYPE_NOTCH);
	m_filterType.SetIntVal(FILTER_TYPE_LOWPASS);

	m_filterLength = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLEDEPTH));
	m_filterLength.SetIntVal(0);

	m_stopbandAtten = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_DB));
	m_stopbandAtten.SetFloatVal(60);

	m_freqLow = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ));
	m_freqLow.SetFloatVal(0);

	m_freqHigh = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ));
	m_freqHigh.SetFloatVal(100e6);

	m_coefficients.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
	m_coefficients.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void FIRFilter::SetDefaultName()
{
	char hwname[256];
	auto type = m_filterType.GetEnumVal<FIRFilterType>();
	switch(type)
	{
		case FILTER_TYPE_LOWPASS:
			snprintf(hwname, sizeof(hwname), "LPF(%s, %s)",
				GetInputDisplayName(0).c_str(),
				m_freqHigh.ToString().c_str());
			break;

		case FILTER_TYPE_HIGHPASS:
			snprintf(hwname, sizeof(hwname), "HPF(%s, %s)",
				GetInputDisplayName(0).c_str(),
				m_freqLow.ToString().c_str());
			break;

		case FILTER_TYPE_BANDPASS:
			snprintf(hwname, sizeof(hwname), "BPF(%s, %s, %s)",
				GetInputDisplayName(0).c_str(),
				m_freqLow.ToString().c_str(),
				m_freqHigh.ToString().c_str());
			break;

		case FILTER_TYPE_NOTCH:
			snprintf(hwname, sizeof(hwname), "Notch(%s, %s, %s)",
				GetInputDisplayName(0).c_str(),
				m_freqLow.ToString().c_str(),
				m_freqHigh.ToString().c_str());
			break;

	}
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string FIRFilter::GetProtocolName()
{
	return "FIR Filter";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void FIRFilter::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("FIR::Refresh");
	#endif

	//Make sure we've got valid inputs
	ClearErrors();
	auto din = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(0));
	if(!din)
	{
		if(!GetInput(0))
			AddErrorMessage("Missing inputs", "No signal input connected");
		else if(!GetInputWaveform(0))
			AddErrorMessage("Missing inputs", "No waveform available at input");
		else
			AddErrorMessage("Invalid inputs", "Expect a uniform analog input");

		SetData(nullptr, 0);
		return;
	}

	//Assume the input is dense packed, get the sample frequency
	int64_t fs_per_sample = din->m_timescale;
	float sample_hz = FS_PER_SECOND / fs_per_sample;

	//Calculate limits for our filter
	float nyquist = sample_hz / 2;
	float flo = m_freqLow.GetFloatVal();
	float fhi = m_freqHigh.GetFloatVal();
	auto type = m_filterType.GetEnumVal<FIRFilterType>();
	if(type == FILTER_TYPE_LOWPASS)
		flo = 0;
	else if(type == FILTER_TYPE_HIGHPASS)
		fhi = nyquist;
	else
	{
		//Swap high/low if they get swapped
		if(fhi < flo)
		{
			float ftmp = flo;
			flo = fhi;
			fhi = ftmp;
		}
	}
	flo = max(flo, 0.0f);
	fhi = min(fhi, nyquist);

	//Calculate filter order
	size_t filterlen = m_filterLength.GetIntVal();
	float atten = m_stopbandAtten.GetFloatVal();
	if(filterlen == 0)
		filterlen = (atten / 22) * (sample_hz / (fhi - flo) );
	filterlen |= 1;	//force length to be odd

	//Don't choke if given an invalid filter configuration
	if(flo == fhi)
	{
		AddErrorMessage("Invalid configuration", "Input and output frequencies are equal");
		SetData(nullptr, 0);
		return;
	}

	//Don't allow filters with more than 4096 taps (probably means something went wrong)
	if(filterlen > 4096)
	{
		AddErrorMessage("Invalid configuration", "Calculated filter kernel has >4096 taps");
		SetData(nullptr, 0);
		return;
	}

	//Create the filter coefficients (TODO: cache this)
	m_coefficients.resize(filterlen);
	CalculateFilterCoefficients(flo / nyquist, fhi / nyquist, atten, type);

	//Set up output
	m_xAxisUnit = m_inputs[0]->GetXAxisUnits();
	SetYAxisUnits(m_inputs[0]->GetYAxisUnits(), 0);
	size_t radius = (filterlen - 1) / 2;
	auto cap = SetupEmptyUniformAnalogOutputWaveform(din, 0);
	cap->Resize(din->size() - filterlen);

	//Run the actual filter
	DoFilterKernel(cmdBuf, queue, din, cap);

	//Shift output to compensate for filter group delay
	cap->m_triggerPhase = (radius * fs_per_sample) + din->m_triggerPhase;
}

void FIRFilter::DoFilterKernel(
	vk::raii::CommandBuffer& cmdBuf,
	shared_ptr<QueueHandle> queue,
	UniformAnalogWaveform* din,
	UniformAnalogWaveform* cap)
{
	cmdBuf.begin({});

	FIRFilterArgs args;
	args.end = din->size() - m_coefficients.size();
	args.filterlen = m_coefficients.size();

	m_computePipeline.BindBufferNonblocking(0, din->m_samples, cmdBuf);
	m_computePipeline.BindBufferNonblocking(1, m_coefficients, cmdBuf);
	m_computePipeline.BindBufferNonblocking(2, cap->m_samples, cmdBuf, true);
	const uint32_t compute_block_count = GetComputeBlockCount(args.end, 64);
	m_computePipeline.Dispatch(cmdBuf, args,
		min(compute_block_count, 32768u),
		compute_block_count / 32768 + 1);

	cmdBuf.end();
	queue->SubmitAndBlock(cmdBuf);

	cap->m_samples.MarkModifiedFromGpu();
}
