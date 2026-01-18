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
#include "AverageFilter.h"

using namespace std;

AverageFilter::AverageFilter(const string& color)
	: Filter(color, CAT_MATH)
{
	AddStream(Unit(Unit::UNIT_VOLTS), "latest", Stream::STREAM_TYPE_ANALOG_SCALAR);
	AddStream(Unit(Unit::UNIT_VOLTS), "cumulative", Stream::STREAM_TYPE_ANALOG_SCALAR);
	AddStream(Unit(Unit::UNIT_SAMPLEDEPTH), "totalSamples", Stream::STREAM_TYPE_ANALOG_SCALAR);
	AddStream(Unit(Unit::UNIT_COUNTS), "totalWaveforms", Stream::STREAM_TYPE_ANALOG_SCALAR);

	m_streams[2].m_flags = Stream::STREAM_INFREQUENTLY_USED;
	m_streams[3].m_flags = Stream::STREAM_INFREQUENTLY_USED;

	CreateInput("in");

	ClearSweeps();
}

AverageFilter::~AverageFilter()
{
}

void AverageFilter::Refresh(vk::raii::CommandBuffer& cmdBuf, shared_ptr<QueueHandle> queue)
{
	ClearErrors();

	auto din = GetInput(0);
	if(!din)
	{
		AddErrorMessage("Missing inputs", "No signal input connected");
		return;
	}

	//Copy units to output streams
	m_streams[0].m_yAxisUnit = din.GetYAxisUnits();
	m_streams[1].m_yAxisUnit = din.GetYAxisUnits();

	//If input is scalar, we are processing a single sample
	if(din.GetType() == Stream::STREAM_TYPE_ANALOG_SCALAR)
	{
		auto vin = din.GetScalarValue();
		m_pastCount ++;
		m_pastSum += vin;

		m_streams[0].m_value = vin;
		m_streams[1].m_value = m_pastSum / m_pastCount;
		m_streams[2].m_value = m_pastCount;
		m_streams[3].m_value = m_pastCount;
	}

	//If input is a vector, process each sample
	else
	{
		auto data = din.GetData();
		if(!data)
		{
			AddErrorMessage("Missing inputs", "No waveform available at input");

			SetData(nullptr, 0);
			return;
		}
		auto udata = dynamic_cast<UniformAnalogWaveform*>(data);
		auto sdata = dynamic_cast<SparseAnalogWaveform*>(data);
		size_t len = data->size();

		float avg;
		if(udata)
			avg = m_averager.Average(udata, cmdBuf, queue);
		else
			avg = m_averager.Average(sdata, cmdBuf, queue);

		//TODO: make Averager output the total as an option?
		float total = avg * len;

		m_pastCount += len;
		m_pastSum += total;

		m_streams[0].m_value = total / len;
		m_streams[1].m_value = m_pastSum / m_pastCount;
		m_streams[2].m_value = m_pastCount;
		m_streams[3].m_value ++;
	}
}

Filter::DataLocation AverageFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

string AverageFilter::GetProtocolName()
{
	return "Average";
}

bool AverageFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(i > 0)
		return false;

	switch(stream.GetType())
	{
		case Stream::STREAM_TYPE_ANALOG:
		case Stream::STREAM_TYPE_ANALOG_SCALAR:
			return true;

		default:
			return false;
	}

	return true;
}

void AverageFilter::ClearSweeps()
{
	m_pastSum = 0;
	m_pastCount = 0;

	m_streams[0].m_value = 0;
	m_streams[1].m_value = 0;
	m_streams[2].m_value = 0;
	m_streams[3].m_value = 0;
}
