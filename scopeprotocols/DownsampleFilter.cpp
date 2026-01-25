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
#include "DownsampleFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DownsampleFilter::DownsampleFilter(const string& color)
	: Filter(color, CAT_MATH)
	, m_decimationFactor(m_parameters["Downsample Factor"])
	, m_aaFilterEnabled(m_parameters["Antialiasing Filter"])
	, m_noAAComputePipeline("shaders/DownsampleNoAAFilter.spv", 2, sizeof(DownsamplePushConstants))
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput("RF");

	m_decimationFactor = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_decimationFactor.SetIntVal(10);

	m_aaFilterEnabled = FilterParameter(FilterParameter::TYPE_BOOL, Unit(Unit::UNIT_COUNTS));
	m_aaFilterEnabled.SetBoolVal(1);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DownsampleFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string DownsampleFilter::GetProtocolName()
{
	return "Downsample";
}

Filter::DataLocation DownsampleFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DownsampleFilter::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("DownsampleFilter::Refresh");
	#endif

	ClearErrors();
	if(!VerifyAllInputsOKAndUniformAnalog())
	{
		if(!GetInput(0))
			AddErrorMessage("Missing inputs", "No signal input connected");
		else if(!GetInputWaveform(0))
			AddErrorMessage("Missing inputs", "No waveform available at input");

		SetData(nullptr, 0);
		return;
	}

	//Get the input data
	auto din = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(0));
	size_t len = din->m_samples.size();

	//Propagate units
	m_streams[0].m_yAxisUnit = GetInput(0).GetYAxisUnits();

	//Validate input configuration
	int64_t factor = m_decimationFactor.GetIntVal();
	if (factor <= 0)
	{
		AddErrorMessage("Invalid configuration", "No waveform available at input");
		return;
	}
	size_t outlen = len / factor;

	//Make output waveform and copy our time scales from the input
	auto cap = SetupEmptyUniformAnalogOutputWaveform(din, 0);
	cap->m_timescale = din->m_timescale * factor;
	cap->Resize(outlen);

	//Push constants for the shader
	DownsamplePushConstants cfg;
	cfg.outlen = outlen;
	cfg.factor = factor;

	//Default path with antialiasing filter
	if(m_aaFilterEnabled.GetBoolVal())
	{
		cap->PrepareForCpuAccess();
		din->PrepareForCpuAccess();

		//Cut off all frequencies shorter than our decimation factor
		float cutoff_period = factor;
		float sigma = cutoff_period / sqrt(2 * log(2));
		int kernel_radius = ceil(3*sigma);

		//Generate the actual Gaussian kernel
		int kernel_size = kernel_radius*2 + 1;
		vector<float> kernel;
		kernel.resize(kernel_size);
		float alpha = 1.0f / (sigma * sqrt(2*M_PI));
		for(int x=0; x < kernel_size; x++)
		{
			int delta = (x - kernel_radius);
			kernel[x] = alpha * exp(-delta*delta/(2*sigma));
		}
		float sum = 0;
		for(auto k : kernel)
			sum += k;
		for(int i=0; i<kernel_size; i++)
			kernel[i] /= sum;

		//Do the actual downsampling.
		for(size_t i=0; i<outlen; i++)
		{
			//Do the convolution
			float conv = 0;
			ssize_t base = i*factor;
			for(ssize_t delta = -kernel_radius; delta <= kernel_radius; delta ++)
			{
				ssize_t pos = base + delta;
				if( (pos < 0) || (pos >= (ssize_t)len) )
					continue;

				conv += din->m_samples[pos] * kernel[delta + kernel_radius];
			}

			//Do the actual decimation
			cap->m_samples[i] 	= conv;
		}

		cap->MarkModifiedFromCpu();
	}

	//Optimized path with no AA if the input is known to not contain any higher frequency content
	else
	{
		cmdBuf.begin({});

		m_noAAComputePipeline.BindBufferNonblocking(0, din->m_samples, cmdBuf);
		m_noAAComputePipeline.BindBufferNonblocking(1, cap->m_samples, cmdBuf, true);
		const uint32_t compute_block_count = GetComputeBlockCount(outlen, 64);
		m_noAAComputePipeline.Dispatch(cmdBuf, cfg,
			min(compute_block_count, 32768u),
			compute_block_count / 32768 + 1);

		cmdBuf.end();
		queue->SubmitAndBlock(cmdBuf);

		cap->m_samples.MarkModifiedFromGpu();
	}
}
