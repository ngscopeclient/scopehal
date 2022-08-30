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

void ThresholdFilter::Refresh()
{
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din = GetInputWaveform(0);
	auto len = din->size();

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

		//Threshold all of our samples
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

		//Threshold all of our samples
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
}
