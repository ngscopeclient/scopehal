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
{
	AddStream(Unit(Unit::UNIT_VOLTS), "sample", Stream::STREAM_TYPE_ANALOG_SCALAR);
	CreateInput("in");

	m_offname = "Sample Point";
	m_parameters[m_offname] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));
	m_parameters[m_offname].SetIntVal(0);
}

PointSampleFilter::~PointSampleFilter()
{
}

void PointSampleFilter::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	auto din = GetInput(0);
	if(!din)
		return;
	auto data = din.GetData();
	if(!data)
	{
		m_streams[0].m_value = std::numeric_limits<float>::quiet_NaN();
		return;
	}

	//Copy units to output streams
	m_streams[0].m_yAxisUnit = din.GetYAxisUnits();
	m_parameters[m_offname].SetUnit(din.GetXAxisUnits());

	//Sample the input
	auto off = m_parameters[m_offname].GetIntVal();
	optional<float> sample = GetValueAtTime(data, off, false);
	if(!sample)
		m_streams[0].m_value = std::numeric_limits<float>::quiet_NaN();
	else
		m_streams[0].m_value = sample.value();
}

Filter::DataLocation PointSampleFilter::GetInputLocation()
{
	return LOC_CPU;
}

string PointSampleFilter::GetProtocolName()
{
	return "Point Sample";
}

bool PointSampleFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(i > 0)
		return false;

	if(stream.GetType() == Stream::STREAM_TYPE_ANALOG)
		return true;
	return false;
}
