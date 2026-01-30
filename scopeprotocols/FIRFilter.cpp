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
	, m_filterTypeName("Filter Type")
	, m_filterLengthName("Length")
	, m_stopbandAttenName("Stopband Attenuation")
	, m_freqLowName("Frequency Low")
	, m_freqHighName("Frequency High")
	, m_computePipeline("shaders/FIRFilter.spv", 3, sizeof(FIRFilterArgs))
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput("in");

	m_parameters[m_filterTypeName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_filterTypeName].AddEnumValue("Low pass", FILTER_TYPE_LOWPASS);
	m_parameters[m_filterTypeName].AddEnumValue("High pass", FILTER_TYPE_HIGHPASS);
	m_parameters[m_filterTypeName].AddEnumValue("Band pass", FILTER_TYPE_BANDPASS);
	m_parameters[m_filterTypeName].AddEnumValue("Notch", FILTER_TYPE_NOTCH);
	m_parameters[m_filterTypeName].SetIntVal(FILTER_TYPE_LOWPASS);

	m_parameters[m_filterLengthName] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLEDEPTH));
	m_parameters[m_filterLengthName].SetIntVal(0);

	m_parameters[m_stopbandAttenName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_DB));
	m_parameters[m_stopbandAttenName].SetFloatVal(60);

	m_parameters[m_freqLowName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ));
	m_parameters[m_freqLowName].SetFloatVal(0);

	m_parameters[m_freqHighName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ));
	m_parameters[m_freqHighName].SetFloatVal(100e6);

	m_coefficients.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
	m_coefficients.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool FIRFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void FIRFilter::SetDefaultName()
{
	char hwname[256];
	auto type = static_cast<FIRFilterType>(m_parameters[m_filterTypeName].GetIntVal());
	switch(type)
	{
		case FILTER_TYPE_LOWPASS:
			snprintf(hwname, sizeof(hwname), "LPF(%s, %s)",
				GetInputDisplayName(0).c_str(),
				m_parameters[m_freqHighName].ToString().c_str());
			break;

		case FILTER_TYPE_HIGHPASS:
			snprintf(hwname, sizeof(hwname), "HPF(%s, %s)",
				GetInputDisplayName(0).c_str(),
				m_parameters[m_freqLowName].ToString().c_str());
			break;

		case FILTER_TYPE_BANDPASS:
			snprintf(hwname, sizeof(hwname), "BPF(%s, %s, %s)",
				GetInputDisplayName(0).c_str(),
				m_parameters[m_freqLowName].ToString().c_str(),
				m_parameters[m_freqHighName].ToString().c_str());
			break;

		case FILTER_TYPE_NOTCH:
			snprintf(hwname, sizeof(hwname), "Notch(%s, %s, %s)",
				GetInputDisplayName(0).c_str(),
				m_parameters[m_freqLowName].ToString().c_str(),
				m_parameters[m_freqHighName].ToString().c_str());
			break;

	}
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string FIRFilter::GetProtocolName()
{
	return "FIR Filter";
}

Filter::DataLocation FIRFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
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
	float flo = m_parameters[m_freqLowName].GetFloatVal();
	float fhi = m_parameters[m_freqHighName].GetFloatVal();
	auto type = static_cast<FIRFilterType>(m_parameters[m_filterTypeName].GetIntVal());
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
	size_t filterlen = m_parameters[m_filterLengthName].GetIntVal();
	float atten = m_parameters[m_stopbandAttenName].GetFloatVal();
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
	m_xAxisUnit = m_inputs[0].m_channel->GetXAxisUnits();
	SetYAxisUnits(m_inputs[0].GetYAxisUnits(), 0);
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
	if(g_gpuFilterEnabled)
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

	else
	{
		din->PrepareForCpuAccess();
		cap->PrepareForCpuAccess();

		DoFilterKernelGeneric(din, cap);

		cap->MarkModifiedFromCpu();
	}
}

/**
	@brief Performs a FIR filter (does not assume symmetric)
 */
void FIRFilter::DoFilterKernelGeneric(
	UniformAnalogWaveform* din,
	UniformAnalogWaveform* cap)
{
	//Setup
	size_t len = din->size();
	size_t filterlen = m_coefficients.size();
	size_t end = len - filterlen;

	//Do the filter
	for(size_t i=0; i<end; i++)
	{
		float v = 0;
		for(size_t j=0; j<filterlen; j++)
			v += din->m_samples[i + j] * m_coefficients[j];

		cap->m_samples[i]	= v;
	}
}
