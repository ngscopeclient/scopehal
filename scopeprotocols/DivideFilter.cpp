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
#include "DivideFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DivideFilter::DivideFilter(const string& color)
	: Filter(color, CAT_MATH)
	, m_formatName("Output Format")
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput("a");
	CreateInput("b");

	m_parameters[m_formatName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_formatName].AddEnumValue("Ratio", FORMAT_RATIO);
	m_parameters[m_formatName].AddEnumValue("dB", FORMAT_DB);
	m_parameters[m_formatName].AddEnumValue("Percent", FORMAT_PERCENT);
	m_parameters[m_formatName].SetIntVal(FORMAT_RATIO);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DivideFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if(i >= 2)
		return false;

	if( (stream.GetType() == Stream::STREAM_TYPE_ANALOG) || (stream.GetType() == Stream::STREAM_TYPE_ANALOG_SCALAR) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string DivideFilter::GetProtocolName()
{
	return "Divide";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DivideFilter::Refresh(vk::raii::CommandBuffer& /*cmdBuf*/, shared_ptr<QueueHandle> /*queue*/)
{
	bool veca = GetInput(0).GetType() == Stream::STREAM_TYPE_ANALOG;
	bool vecb = GetInput(1).GetType() == Stream::STREAM_TYPE_ANALOG;

	if(veca && vecb)
		DoRefreshVectorVector();
	else if(!veca && !vecb)
		DoRefreshScalarScalar();
	else if(veca)
		RefreshScalarVector(1, 0);
	else
		RefreshScalarVector(0, 1);
}

void DivideFilter::DoRefreshScalarScalar()
{
	m_streams[0].m_stype = Stream::STREAM_TYPE_ANALOG_SCALAR;
	SetData(nullptr, 0);

	//Different output formats possible besides just a direct division
	switch(m_parameters[m_formatName].GetIntVal())
	{
		case FORMAT_RATIO:
			SetYAxisUnits(GetInput(0).GetYAxisUnits() / GetInput(1).GetYAxisUnits(), 0);
			m_streams[0].m_value = GetInput(0).GetScalarValue() / GetInput(1).GetScalarValue();
			break;

		case FORMAT_PERCENT:
			SetYAxisUnits(Unit(Unit::UNIT_PERCENT), 0);
			m_streams[0].m_value = GetInput(0).GetScalarValue() / GetInput(1).GetScalarValue();
			break;

		case FORMAT_DB:
			SetYAxisUnits(Unit(Unit::UNIT_DB), 0);
			m_streams[0].m_value = 20 * log10(GetInput(0).GetScalarValue() / GetInput(1).GetScalarValue());
			break;

		default:
			break;
	}
}

void DivideFilter::RefreshScalarVector(size_t iScalar, size_t iVector)
{
	m_streams[0].m_stype = Stream::STREAM_TYPE_ANALOG;

	float scale = GetInput(iScalar).GetScalarValue();
	auto din = GetInputWaveform(iVector);
	if(!din)
	{
		SetData(nullptr, 0);
		return;
	}
	din->PrepareForCpuAccess();
	auto len = din->size();

	auto sparse = dynamic_cast<SparseAnalogWaveform*>(din);
	auto uniform = dynamic_cast<UniformAnalogWaveform*>(din);

	//TODO: support different output formats

	if(sparse)
	{
		//Set up the output waveform
		auto cap = SetupSparseOutputWaveform(sparse, 0, 0, 0);
		cap->Resize(len);
		cap->PrepareForCpuAccess();

		float* fin = (float*)__builtin_assume_aligned(sparse->m_samples.GetCpuPointer(), 16);
		float* fdst = (float*)__builtin_assume_aligned(cap->m_samples.GetCpuPointer(), 16);
		if(iVector == 0)
		{
			for(size_t i=0; i<len; i++)
				fdst[i] = fin[i] / scale;
		}
		else
		{
			for(size_t i=0; i<len; i++)
				fdst[i] = scale / fin[i];
		}

		cap->MarkModifiedFromCpu();
	}
	else
	{
		//Set up the output waveform
		auto cap = SetupEmptyUniformAnalogOutputWaveform(uniform, 0);
		cap->Resize(len);
		cap->PrepareForCpuAccess();

		float* fin = (float*)__builtin_assume_aligned(uniform->m_samples.GetCpuPointer(), 16);
		float* fdst = (float*)__builtin_assume_aligned(cap->m_samples.GetCpuPointer(), 16);
		if(iVector == 0)
		{
			for(size_t i=0; i<len; i++)
				fdst[i] = fin[i] / scale;
		}
		else
		{
			for(size_t i=0; i<len; i++)
				fdst[i] = scale / fin[i];
		}

		cap->MarkModifiedFromCpu();
	}
}

void DivideFilter::DoRefreshVectorVector()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	//For now, only implement uniform analog
	auto a = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(0));
	auto b = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(0));
	if(!a || !b)
	{
		SetData(NULL, 0);
		return;
	}
	auto len = min(a->size(), b->size());

	a->PrepareForCpuAccess();
	b->PrepareForCpuAccess();

	//Set up the output waveform
	auto cap = SetupEmptyUniformAnalogOutputWaveform(a, 0);
	cap->PrepareForCpuAccess();

	float* fa = (float*)__builtin_assume_aligned(&a->m_samples[0], 16);
	float* fb = (float*)__builtin_assume_aligned(&b->m_samples[0], 16);
	float* fdst = (float*)__builtin_assume_aligned(&cap->m_samples[0], 16);

	size_t i=0;
	switch(m_parameters[m_formatName].GetIntVal())
	{
		case FORMAT_RATIO:
			SetYAxisUnits(GetInput(0).GetYAxisUnits() / GetInput(1).GetYAxisUnits(), 0);
			for(; i<len; i++)
				fdst[i] = fa[i] / fb[i];
			break;

		case FORMAT_PERCENT:
			SetYAxisUnits(Unit(Unit::UNIT_PERCENT), 0);
			for(; i<len; i++)
				fdst[i] = fa[i] / fb[i];
			break;

		case FORMAT_DB:
			SetYAxisUnits(Unit(Unit::UNIT_DB), 0);

			for(i=0; i<len; i++)
				fdst[i] = 20 * log10(fa[i] / fb[i]);
			break;

		default:
			break;

	}

	cap->MarkModifiedFromCpu();
}
