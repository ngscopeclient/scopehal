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
#include "PAMEdgeDetectorFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PAMEdgeDetectorFilter::PAMEdgeDetectorFilter(const string& color)
	: Filter(color, CAT_CLOCK)
	, m_order("PAM Order")
{
	AddDigitalStream("data");
	CreateInput("din");

	/*
	m_threshname = "PAMEdgeDetector";
	m_parameters[m_threshname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_threshname].SetFloatVal(0);
	*/

	m_parameters[m_order] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_order].SetIntVal(3);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool PAMEdgeDetectorFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string PAMEdgeDetectorFilter::GetProtocolName()
{
	return "PAM Edge Detector";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void PAMEdgeDetectorFilter::Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue)
{
	if(!VerifyAllInputsOK())
	{
		SetData(nullptr, 0);
		return;
	}

	//Get the input data
	auto din = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(0));
	if(!din)
	{
		SetData(nullptr, 0);
		return;
	}
	auto len = din->size();

	//Find min/max of the input
	size_t order = m_parameters[m_order].GetIntVal();
	din->PrepareForCpuAccess();
	auto vmin = GetMinVoltage(din);
	auto vmax = GetMaxVoltage(din);
	Unit v(Unit::UNIT_VOLTS);
	LogTrace("Bounds are %s to %s\n", v.PrettyPrint(vmin).c_str(), v.PrettyPrint(vmax).c_str());

	//Take a histogram and find the top N peaks (should be roughly evenly distributed)
	const int64_t nbins = 100;
	auto peaks = MakeHistogram(din, vmin, vmax, nbins);
	float binsize = (vmax - vmin) / bins;

	//Search radius for bins (for now hard code, TODO make this adaptive?)
	const int64_t searchrad = 4;

	/*
	//Setup
	float midpoint = m_parameters[m_threshname].GetFloatVal();
	float hys = m_parameters[m_hysname].GetFloatVal();

	din->PrepareForCpuAccess();

	auto sdin = dynamic_cast<SparseAnalogWaveform*>(din);
	auto udin = dynamic_cast<UniformAnalogWaveform*>(din);

	if(sdin)
	{
		auto cap = SetupSparseDigitalOutputWaveform(sdin, 0, 0, 0);
		cap->PrepareForCpuAccess();

		//PAMEdgeDetector all of our samples
		//Optimized inner loop if no hysteresis
		if(hys == 0)
		{
			#pragma omp parallel for
			for(size_t i=0; i<len; i++)
				cap->m_samples[i] = sdin->m_samples[i] > midpoint;
		}
		else
		{
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
		}

		cap->MarkModifiedFromCpu();
	}
	else
	{
		auto cap = SetupEmptyUniformDigitalOutputWaveform(din, 0);
		cap->Resize(len);
		cap->PrepareForCpuAccess();

		//PAMEdgeDetector all of our samples
		//Optimized inner loop if no hysteresis
		if(hys == 0)
		{
			#pragma omp parallel for
			for(size_t i=0; i<len; i++)
				cap->m_samples[i] = udin->m_samples[i] > midpoint;
		}
		else
		{
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
		}

		cap->MarkModifiedFromCpu();
	}
	*/
}
