/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2025 Andrew D. Zonenberg and contributors                                                         *
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
#include "PointSampleFilter.h"
#include <limits>

using namespace std;

PointSampleFilter::PointSampleFilter(const string& color)
	: Filter(color, CAT_MATH)
	, m_off(m_parameters["Sample Point"])
{
	AddStream(Unit(Unit::UNIT_VOLTS), "sample", Stream::STREAM_TYPE_ANALOG_SCALAR);
	CreateInput<InputConstraintStreamType>("in", Stream::STREAM_TYPE_ANALOG);

	m_off = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));
	m_off.SetIntVal(0);
}

PointSampleFilter::~PointSampleFilter()
{
}

void PointSampleFilter::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("OneWireDecoder::Refresh");
	#endif
	ClearErrors();

	auto din = GetInput(0);
	if(!din)
	{
		AddErrorMessage("Missing input", "One or more inputs are unconnected");
		return;
	}

	auto data = din.GetData();
	if(!data)
	{
		m_streams[0].m_value = std::numeric_limits<float>::quiet_NaN();
		AddErrorMessage("Missing data", "Input is null");
		return;
	}

	//Grab the input to the CPU
	//TODO: only grab a single sample of interest
	cmdBuf.begin({});
		data->PrepareForCpuAccessNonblocking(cmdBuf);
	cmdBuf.end();
	queue->SubmitAndBlock(cmdBuf);

	//Copy units to output streams
	m_streams[0].m_yAxisUnit = din.GetYAxisUnits();
	m_off.SetUnit(din.GetXAxisUnits());

	//Sample the input
	auto off = m_off.GetIntVal();
	optional<float> sample = GetValueAtTime(data, off, false);
	if(!sample)
		m_streams[0].m_value = std::numeric_limits<float>::quiet_NaN();
	else
		m_streams[0].m_value = sample.value();
}

string PointSampleFilter::GetProtocolName()
{
	return "Point Sample";
}
