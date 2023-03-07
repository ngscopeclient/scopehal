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
#include "SubtractFilter.h"
#ifdef __x86_64__
#include <immintrin.h>
#endif

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SubtractFilter::SubtractFilter(const string& color)
	: Filter(color, CAT_MATH)
	, m_computePipeline("shaders/SubtractFilter.spv", 3, sizeof(uint32_t))
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput("IN+");
	CreateInput("IN-");
}

SubtractFilter::~SubtractFilter()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool SubtractFilter::ValidateChannel(size_t i, StreamDescriptor stream)
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

void SubtractFilter::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "%s - %s",
		GetInputDisplayName(0).c_str(),
		GetInputDisplayName(1).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string SubtractFilter::GetProtocolName()
{
	return "Subtract";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void SubtractFilter::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	bool veca = GetInput(0).GetType() == Stream::STREAM_TYPE_ANALOG;
	bool vecb = GetInput(1).GetType() == Stream::STREAM_TYPE_ANALOG;

	if(veca && vecb)
		DoRefreshVectorVector(cmdBuf, queue);
	else if(!veca && !vecb)
		DoRefreshScalarScalar();
	else
	{
		LogWarning("[SubtractFilter::Refresh] Scalar - vector case not yet implemented\n");
		SetData(nullptr, 0);
	}
}

void SubtractFilter::DoRefreshScalarScalar()
{
	m_streams[0].m_stype = Stream::STREAM_TYPE_ANALOG_SCALAR;
	SetData(nullptr, 0);

	//Subtract value
	//TODO: how to handle unequal units?
	m_streams[0].m_yAxisUnit = GetInput(0).GetYAxisUnits();
	m_streams[0].m_value = GetInput(0).GetScalarValue() - GetInput(1).GetScalarValue();
}

void SubtractFilter::DoRefreshVectorVector(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue)
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get inputs
	auto din_p = GetInputWaveform(0);
	auto din_n = GetInputWaveform(1);
	auto sdin_p = dynamic_cast<SparseAnalogWaveform*>(din_p);
	auto sdin_n = dynamic_cast<SparseAnalogWaveform*>(din_n);
	auto udin_p = dynamic_cast<UniformAnalogWaveform*>(din_p);
	auto udin_n = dynamic_cast<UniformAnalogWaveform*>(din_n);

	//Set up units and complain if they're inconsistent
	m_xAxisUnit = m_inputs[0].m_channel->GetXAxisUnits();
	SetYAxisUnits(m_inputs[0].GetYAxisUnits(), 0);
	if( (m_xAxisUnit != m_inputs[1].m_channel->GetXAxisUnits()) ||
		(m_inputs[0].GetYAxisUnits() != m_inputs[1].GetYAxisUnits()) )
	{
		SetData(NULL, 0);
		return;
	}

	//We need meaningful data
	size_t len = min(din_p->size(), din_n->size());

	//Setup output waveform
	UniformAnalogWaveform* ucap = nullptr;
	SparseAnalogWaveform* scap = nullptr;
	if(sdin_p && sdin_n)
		scap = SetupSparseOutputWaveform(sdin_p, 0, 0, 0);
	else if(udin_p && udin_n)
	{
		ucap = SetupEmptyUniformAnalogOutputWaveform(udin_p, 0);
		ucap->Resize(len);
	}

	//Mixed sparse/uniform not allowed
	else
	{
		SetData(NULL, 0);
		return;
	}

	//Special case if input units are degrees: we want to do modular arithmetic
	//TODO: vectorized version of this
	if(GetYAxisUnits(0) == Unit::UNIT_DEGREES)
	{
		//Waveform data must be on CPU
		din_p->PrepareForCpuAccess();
		din_n->PrepareForCpuAccess();
		if(scap)
			scap->PrepareForCpuAccess();
		else
			ucap->PrepareForCpuAccess();

		float* out = scap ? scap->m_samples.GetCpuPointer() : ucap->m_samples.GetCpuPointer();
		float* a = sdin_p ? sdin_p->m_samples.GetCpuPointer() : udin_p->m_samples.GetCpuPointer();
		float* b = sdin_n ? sdin_n->m_samples.GetCpuPointer() : udin_n->m_samples.GetCpuPointer();

		for(size_t i=0; i<len; i++)
		{
			out[i] 		= a[i] - b[i];
			if(out[i] < -180)
				out[i] += 360;
			if(out[i] > 180)
				out[i] -= 360;
		}

		if(scap)
			scap->m_samples.MarkModifiedFromCpu();
		else
			ucap->m_samples.MarkModifiedFromCpu();
	}

	//Just regular subtraction, use the GPU filter
	else if(g_gpuFilterEnabled)
	{
		cmdBuf.begin({});

		m_computePipeline.BindBufferNonblocking(0, sdin_p ? sdin_p->m_samples : udin_p->m_samples, cmdBuf);
		m_computePipeline.BindBufferNonblocking(1, sdin_n ? sdin_n->m_samples : udin_n->m_samples, cmdBuf);
		m_computePipeline.BindBufferNonblocking(2, scap ? scap->m_samples : ucap->m_samples, cmdBuf, true);
		m_computePipeline.Dispatch(cmdBuf, (uint32_t)len, GetComputeBlockCount(len, 64));

		cmdBuf.end();
		queue->SubmitAndBlock(cmdBuf);

		if(scap)
			scap->m_samples.MarkModifiedFromGpu();
		else
			ucap->m_samples.MarkModifiedFromGpu();
	}

	//Software fallback
	else
	{
		//Waveform data must be on CPU
		din_p->PrepareForCpuAccess();
		din_n->PrepareForCpuAccess();
		if(scap)
			scap->PrepareForCpuAccess();
		else
			ucap->PrepareForCpuAccess();

		float* out = scap ? scap->m_samples.GetCpuPointer() : ucap->m_samples.GetCpuPointer();
		float* a = sdin_p ? sdin_p->m_samples.GetCpuPointer() : udin_p->m_samples.GetCpuPointer();
		float* b = sdin_n ? sdin_n->m_samples.GetCpuPointer() : udin_n->m_samples.GetCpuPointer();

		#ifdef __x86_64__
		if(g_hasAvx2)
			InnerLoopAVX2(out, a, b, len);
		else
		#endif
			InnerLoop(out, a, b, len);

		if(scap)
			scap->m_samples.MarkModifiedFromCpu();
		else
			ucap->m_samples.MarkModifiedFromCpu();
	}
}

//We probably still have SSE2 or similar if no AVX, so give alignment hints for compiler auto-vectorization
void SubtractFilter::InnerLoop(float* out, float* a, float* b, size_t len)
{
	out = (float*)__builtin_assume_aligned(out, 64);
	a = (float*)__builtin_assume_aligned(a, 64);
	b = (float*)__builtin_assume_aligned(b, 64);

	for(size_t i=0; i<len; i++)
		out[i] 		= a[i] - b[i];
}

#ifdef __x86_64__
__attribute__((target("avx2")))
void SubtractFilter::InnerLoopAVX2(float* out, float* a, float* b, size_t len)
{
	size_t end = len - (len % 8);

	//AVX2
	for(size_t i=0; i<end; i+=8)
	{
		__m256 pa = _mm256_load_ps(a + i);
		__m256 pb = _mm256_load_ps(b + i);
		__m256 o = _mm256_sub_ps(pa, pb);
		_mm256_store_ps(out+i, o);
	}

	//Get any extras
	for(size_t i=end; i<len; i++)
		out[i] 		= a[i] - b[i];
}
#endif /* __x86_64__ */

Filter::DataLocation SubtractFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}
