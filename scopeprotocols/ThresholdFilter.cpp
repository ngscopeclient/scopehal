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
#include "ThresholdFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ThresholdFilter::ThresholdFilter(const string& color)
	: Filter(color, CAT_MATH)
{
	AddDigitalStream("data");
	CreateInput("din");

	m_threshname = "Threshold";
	m_parameters[m_threshname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_threshname].SetFloatVal(0);

	m_hysname = "Hysteresis";
	m_parameters[m_hysname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_hysname].SetFloatVal(0);

	if(g_hasShaderInt8)
	{
		m_computePipeline = make_unique<ComputePipeline>(
			"shaders/Threshold.spv",
			2,
			sizeof(ThresholdPushConstants));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool ThresholdFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string ThresholdFilter::GetProtocolName()
{
	return "Threshold";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

Filter::DataLocation ThresholdFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

void ThresholdFilter::Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range range("ThresholdFilter::Refresh");
	#endif

	if(!VerifyAllInputsOK())
	{
		SetData(nullptr, 0);
		return;
	}

	//Get the input data
	auto din = GetInputWaveform(0);
	auto len = din->size();

	//Setup
	float midpoint = m_parameters[m_threshname].GetFloatVal();
	float hys = m_parameters[m_hysname].GetFloatVal();

	auto sdin = dynamic_cast<SparseAnalogWaveform*>(din);
	auto udin = dynamic_cast<UniformAnalogWaveform*>(din);

	if(sdin)
	{
		auto cap = SetupSparseDigitalOutputWaveform(sdin, 0, 0, 0);

		//Threshold all of our samples
		//Optimized inner loop if no hysteresis
		if(hys == 0)
		{
			if(g_hasShaderInt8)
			{
				cmdBuf.begin({});

				ThresholdPushConstants tpush;
				tpush.numSamples	= sdin->m_samples.size();
				tpush.threshold		= midpoint;

				m_computePipeline->BindBufferNonblocking(0, cap->m_samples, cmdBuf, true);
				m_computePipeline->BindBufferNonblocking(1, sdin->m_samples, cmdBuf);

				const uint32_t compute_block_count = GetComputeBlockCount(tpush.numSamples, 64);
				m_computePipeline->Dispatch(cmdBuf, tpush,
					min(compute_block_count, 32768u),
					compute_block_count / 32768 + 1);

				cmdBuf.end();
				queue->SubmitAndBlock(cmdBuf);

				cap->MarkModifiedFromGpu();
			}
			else
			{
				din->PrepareForCpuAccess();
				cap->PrepareForCpuAccess();

				#pragma omp parallel for
				for(size_t i=0; i<len; i++)
					cap->m_samples[i] = sdin->m_samples[i] > midpoint;

				cap->MarkModifiedFromCpu();
			}
		}
		else
		{
			din->PrepareForCpuAccess();
			cap->PrepareForCpuAccess();

			bool cur = sdin->m_samples[0] > midpoint;
			float thresh_rising = midpoint + hys/2;
			float thresh_falling = midpoint - hys/2;

			for(size_t i=0; i<len; i++)
			{
				float f = sdin->m_samples[i];
				if(cur && (f < thresh_falling))
					cur = false;
				else if(!cur && (f > thresh_rising))
					cur = true;
				cap->m_samples[i] = cur;
			}

			cap->MarkModifiedFromCpu();
		}
	}
	else
	{
		auto cap = SetupEmptyUniformDigitalOutputWaveform(din, 0);
		cap->Resize(len);

		//Threshold all of our samples
		//Optimized inner loop if no hysteresis
		if(hys == 0)
		{
			if(g_hasShaderInt8)
			{
				cmdBuf.begin({});

				ThresholdPushConstants tpush;
				tpush.numSamples	= udin->m_samples.size();
				tpush.threshold		= midpoint;

				m_computePipeline->BindBufferNonblocking(0, cap->m_samples, cmdBuf, true);
				m_computePipeline->BindBufferNonblocking(1, udin->m_samples, cmdBuf);

				const uint32_t compute_block_count = GetComputeBlockCount(tpush.numSamples, 64);
				m_computePipeline->Dispatch(cmdBuf, tpush,
					min(compute_block_count, 32768u),
					compute_block_count / 32768 + 1);

				cmdBuf.end();
				queue->SubmitAndBlock(cmdBuf);

				cap->MarkModifiedFromGpu();
			}
			else
			{
				din->PrepareForCpuAccess();
				cap->PrepareForCpuAccess();

				#pragma omp parallel for
				for(size_t i=0; i<len; i++)
					cap->m_samples[i] = udin->m_samples[i] > midpoint;

				cap->MarkModifiedFromCpu();
			}
		}
		else
		{
			din->PrepareForCpuAccess();
			cap->PrepareForCpuAccess();

			bool cur = udin->m_samples[0] > midpoint;
			float thresh_rising = midpoint + hys/2;
			float thresh_falling = midpoint - hys/2;

			for(size_t i=0; i<len; i++)
			{
				float f = udin->m_samples[i];
				if(cur && (f < thresh_falling))
					cur = false;
				else if(!cur && (f > thresh_rising))
					cur = true;
				cap->m_samples[i] = cur;
			}

			cap->MarkModifiedFromCpu();
		}
	}
}
