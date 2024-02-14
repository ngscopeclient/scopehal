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
#include "ConstellationFilter.h"
#include <algorithm>
#ifdef __x86_64__
#include <immintrin.h>
#endif

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ConstellationFilter::ConstellationFilter(const string& color)
	: Filter(color, CAT_RF)
	, m_height(1)
	, m_width(1)
	, m_xscale(0)
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_CONSTELLATION);

	m_xAxisUnit = Unit(Unit::UNIT_MICROVOLTS);

	CreateInput("i");
	CreateInput("q");
	CreateInput("clk");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool ConstellationFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 2) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;
	if( (i == 2) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string ConstellationFilter::GetProtocolName()
{
	return "Constellation";
}

float ConstellationFilter::GetVoltageRange(size_t /*stream*/)
{
	return m_inputs[0].GetVoltageRange();
}

float ConstellationFilter::GetOffset(size_t /*stream*/)
{
	return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void ConstellationFilter::ClearSweeps()
{
	SetData(NULL, 0);
}

void ConstellationFilter::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	if(!VerifyAllInputsOK())
	{
		//if input goes momentarily bad, don't delete output - just stop updating
		return;
	}

	auto din_i = GetInputWaveform(0);
	auto din_q = GetInputWaveform(1);
	auto clk = GetInputWaveform(2);

	//Sample the I/Q input
	SparseAnalogWaveform samples_i;
	SparseAnalogWaveform samples_q;
	SampleOnAnyEdgesBase(din_i, clk, samples_i);
	SampleOnAnyEdgesBase(din_q, clk, samples_q);

	size_t inlen = min(samples_i.size(), samples_q.size());

	//Generate the output waveform
	auto cap = dynamic_cast<ConstellationWaveform*>(GetData(0));
	if(!cap)
		cap = ReallocateWaveform();
	cap->PrepareForCpuAccess();

	//Recompute scales
	float xscale = m_width / GetVoltageRange(0);
	float xmid = m_width / 2;
	float yscale = m_height / GetVoltageRange(0);
	float ymid = m_height / 2;

	//Actual integration loop
	//TODO: vectorize, GPU, or both?
	auto data = cap->GetAccumData();
	for(size_t i=0; i<inlen; i++)
	{
		ssize_t x = static_cast<ssize_t>(round(xmid + xscale * samples_i.m_samples[i]));
		ssize_t y = static_cast<ssize_t>(round(ymid + yscale * samples_q.m_samples[i]));

		//bounds check
		if( (x < 0) || (x >= (ssize_t)m_width) || (y < 0) || (y >= (ssize_t)m_height) )
			continue;

		//fill
		data[y*m_width + x] ++;
	}

	//Count total number of symbols we've integrated
	cap->IntegrateSymbols(inlen);
	cap->Normalize();
}

ConstellationWaveform* ConstellationFilter::ReallocateWaveform()
{
	auto cap = new ConstellationWaveform(m_width, m_height);
	cap->m_timescale = 1;
	SetData(cap, 0);
	return cap;
}
