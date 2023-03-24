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

#include "../scopehal/scopehal.h"
#include "MovingAverageFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

MovingAverageFilter::MovingAverageFilter(const string& color)
	: Filter(color, CAT_MATH)
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput("din");

	m_depthname = "Depth";
	m_parameters[m_depthname] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLEDEPTH));
	m_parameters[m_depthname].SetFloatVal(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool MovingAverageFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string MovingAverageFilter::GetProtocolName()
{
	return "Moving average";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void MovingAverageFilter::Refresh()
{
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din = GetInputWaveform(0);
	din->PrepareForCpuAccess();
	size_t len = din->size();
	size_t depth = m_parameters[m_depthname].GetIntVal();
	if(len < depth)
	{
		SetData(NULL, 0);
		return;
	}

	m_xAxisUnit = m_inputs[0].m_channel->GetXAxisUnits();
	SetYAxisUnits(m_inputs[0].GetYAxisUnits(), 0);

	size_t off = depth/2;
	size_t nsamples = len - 2*off;

	auto sdin = dynamic_cast<SparseAnalogWaveform*>(din);
	auto udin = dynamic_cast<UniformAnalogWaveform*>(din);

	if(sdin)
	{
		//Do the average
		auto cap = SetupSparseOutputWaveform(sdin, 0, off, off);
		cap->PrepareForCpuAccess();

		//#pragma omp parallel for
		for(size_t i=0; i<nsamples; i++)
		{
			float v = 0;
			for(size_t j=0; j<depth; j++)
				v += sdin->m_samples[i+j];
			v /= depth;

			cap->m_offsets[i] = sdin->m_offsets[i+off];
			cap->m_durations[i] = sdin->m_durations[i+off];
			cap->m_samples[i] = v;
		}
		SetData(cap, 0);

		cap->MarkModifiedFromCpu();
	}
	else
	{
		//Do the average
		auto cap = SetupEmptyUniformAnalogOutputWaveform(udin, 0);
		cap->PrepareForCpuAccess();
		cap->Resize(nsamples);
		//#pragma omp parallel for
		for(size_t i=0; i<nsamples; i++)
		{
			float v = 0;
			for(size_t j=0; j<depth; j++)
				v += udin->m_samples[i+j];
			v /= depth;

			cap->m_samples[i] = v;
		}
		SetData(cap, 0);

		cap->MarkModifiedFromCpu();
	}
}
