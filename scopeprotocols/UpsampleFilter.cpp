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

#ifdef _WIN32
#include <cmath>
#endif

#include "../scopehal/scopehal.h"
#include "UpsampleFilter.h"

using namespace std;

float sinc(float x, float width);
float blackman(float x, float width);

float sinc(float x, float width)
{
	float xi = x - width/2;

	if(fabs(xi) < 1e-7)
		return 1.0f;
	else
	{
		float px = M_PI*xi;
		return sin(px) / px;
	}
}

float blackman(float x, float width)
{
	if(x > width)
		return 0;
	return 0.42 - 0.5*cos(2*M_PI * x / width) + 0.08 * cos(4*M_PI*x/width);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

UpsampleFilter::UpsampleFilter(const string& color)
	: Filter(color, CAT_MATH)
	, m_computePipeline("shaders/UpsampleFilter.spv", 3, sizeof(UpsampleFilterArgs))
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput("din");

	m_factorname = "Upsample factor";
	m_parameters[m_factorname] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLEDEPTH));
	m_parameters[m_factorname].SetIntVal(10);

	//Use pinned memory for filter kernel
	m_filter.SetCpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
	m_filter.SetGpuAccessHint(AcceleratorBuffer<float>::HINT_LIKELY);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool UpsampleFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string UpsampleFilter::GetProtocolName()
{
	return "Upsample";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void UpsampleFilter::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	//Get the input data
	//Current resampling implementation assumes input is uniform, fail if it's not
	auto din = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(0));
	if(!din)
	{
		SetData(NULL, 0);
		return;
	}

	//Configuration parameters (TODO: allow window to be user specified)
	size_t upsample_factor = m_parameters[m_factorname].GetIntVal();
	size_t window = 5;
	size_t kernel = window*upsample_factor;
	if(upsample_factor <= 0)
	{
		SetData(NULL, 0);
		return;
	}

	//Create the interpolation filter
	//TODO: if upsampling factor and window size have not changed, keep the same filter coefficients
	//(no need to push every time)
	float frac_kernel = kernel * 1.0f / upsample_factor;
	m_filter.resize(kernel);
	m_filter.PrepareForCpuAccess();
	for(size_t i=0; i<kernel; i++)
	{
		float frac = i*1.0f / upsample_factor;
		m_filter[i] = sinc(frac, frac_kernel) * blackman(frac, frac_kernel);
	}
	m_filter.MarkModifiedFromCpu();

	//Create the output and configure it
	auto cap = SetupEmptyUniformAnalogOutputWaveform(din, 0);
	cap->m_timescale = din->m_timescale / upsample_factor;
	size_t len = din->size();
	size_t imax = len - window;
	size_t outlen = imax*upsample_factor;
	cap->Resize(outlen);

	if(g_gpuFilterEnabled)
	{
		cmdBuf.begin({});

		//Update our descriptor sets with current buffers
		m_computePipeline.BindBufferNonblocking(0, din->m_samples, cmdBuf);
		m_computePipeline.BindBufferNonblocking(1, m_filter, cmdBuf);
		m_computePipeline.BindBufferNonblocking(2, cap->m_samples, cmdBuf, true);

		UpsampleFilterArgs args;
		args.imax = imax;
		args.upsample_factor = upsample_factor;
		args.kernel = kernel;

		m_computePipeline.Dispatch(cmdBuf, args, GetComputeBlockCount(imax, 64), GetComputeBlockCount(upsample_factor, 1));

		//Done, submit to the queue and wait
		cmdBuf.end();
		queue->SubmitAndBlock(cmdBuf);
		cap->MarkModifiedFromGpu();
	}

	else
	{
		din->PrepareForCpuAccess();
		cap->PrepareForCpuAccess();

		//Logically, we upsample by inserting zeroes, then convolve with the sinc filter.
		//Optimization: don't actually waste time multiplying by zero
		#pragma omp parallel for
		for(size_t i=0; i < imax; i++)
		{
			size_t offset = i * upsample_factor;
			for(size_t j=0; j<upsample_factor; j++)
			{
				size_t start = 0;
				size_t sstart = 0;
				if(j > 0)
				{
					sstart = 1;
					start = upsample_factor - j;
				}

				float f = 0;
				for(size_t k = start; k<kernel; k += upsample_factor, sstart ++)
					f += m_filter[k] * din->m_samples[i + sstart];

				cap->m_samples[offset + j] = f;
			}
		}

		cap->MarkModifiedFromCpu();
	}
}

Filter::DataLocation UpsampleFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

