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
#include "MultiplyFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

MultiplyFilter::MultiplyFilter(const string& color)
	: Filter(color, CAT_MATH)
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput("a");
	CreateInput("b");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool MultiplyFilter::ValidateChannel(size_t i, StreamDescriptor stream)
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

string MultiplyFilter::GetProtocolName()
{
	return "Multiply";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void MultiplyFilter::Refresh()
{
	bool veca = GetInput(0).GetType() == Stream::STREAM_TYPE_ANALOG;
	bool vecb = GetInput(1).GetType() == Stream::STREAM_TYPE_ANALOG;

	//Update units
	if(m_inputs[0] && m_inputs[1])
		SetYAxisUnits(m_inputs[0].GetYAxisUnits() * m_inputs[1].GetYAxisUnits(), 0);

	if(veca && vecb)
		RefreshVectorVector();
	else if(!veca && !vecb)
		RefreshScalarScalar();
	else if(veca)
		RefreshScalarVector(1, 0);
	else
		RefreshScalarVector(0, 1);
}

void MultiplyFilter::RefreshScalarScalar()
{
	m_streams[0].m_stype = Stream::STREAM_TYPE_ANALOG_SCALAR;
	SetData(nullptr, 0);

	//Multiply value
	m_streams[0].m_value = GetInput(0).GetScalarValue() * GetInput(1).GetScalarValue();
}

void MultiplyFilter::RefreshScalarVector(size_t iScalar, size_t iVector)
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

	if(sparse)
	{
		//Set up the output waveform
		auto cap = SetupSparseOutputWaveform(sparse, 0, 0, 0);
		cap->Resize(len);
		cap->PrepareForCpuAccess();

		float* fin = (float*)__builtin_assume_aligned(sparse->m_samples.GetCpuPointer(), 16);
		float* fdst = (float*)__builtin_assume_aligned(cap->m_samples.GetCpuPointer(), 16);
		for(size_t i=0; i<len; i++)
			fdst[i] = fin[i] * scale;

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
		for(size_t i=0; i<len; i++)
			fdst[i] = fin[i] * scale;

		cap->MarkModifiedFromCpu();
	}
}

void MultiplyFilter::RefreshVectorVector()
{
	m_streams[0].m_stype = Stream::STREAM_TYPE_ANALOG;

	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(nullptr, 0);
		return;
	}

	//Get the input data
	auto a = GetInputWaveform(0);
	auto b = GetInputWaveform(1);
	auto len = min(a->size(), b->size());
	a->PrepareForCpuAccess();
	b->PrepareForCpuAccess();

	//Type conversion
	auto sa = dynamic_cast<SparseAnalogWaveform*>(a);
	auto sb = dynamic_cast<SparseAnalogWaveform*>(b);
	auto ua = dynamic_cast<UniformAnalogWaveform*>(a);
	auto ub = dynamic_cast<UniformAnalogWaveform*>(b);

	if(sa && sb)
	{
		//Set up the output waveform
		auto cap = SetupSparseOutputWaveform(sa, 0, 0, 0);
		cap->Resize(len);
		cap->PrepareForCpuAccess();

		float* fa = (float*)__builtin_assume_aligned(sa->m_samples.GetCpuPointer(), 16);
		float* fb = (float*)__builtin_assume_aligned(sb->m_samples.GetCpuPointer(), 16);
		float* fdst = (float*)__builtin_assume_aligned(cap->m_samples.GetCpuPointer(), 16);
		for(size_t i=0; i<len; i++)
			fdst[i] = fa[i] * fb[i];

		cap->MarkModifiedFromCpu();
	}
	else if(ua && ub)
	{
		//Set up the output waveform
		auto cap = SetupEmptyUniformAnalogOutputWaveform(ua, 0);
		cap->Resize(len);
		cap->PrepareForCpuAccess();

		float* fa = (float*)__builtin_assume_aligned(ua->m_samples.GetCpuPointer(), 16);
		float* fb = (float*)__builtin_assume_aligned(ub->m_samples.GetCpuPointer(), 16);
		float* fdst = (float*)__builtin_assume_aligned(cap->m_samples.GetCpuPointer(), 16);
		for(size_t i=0; i<len; i++)
			fdst[i] = fa[i] * fb[i];

		cap->MarkModifiedFromCpu();
	}
}
