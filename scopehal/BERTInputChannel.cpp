/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
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

#include "scopehal.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

BERTInputChannel::BERTInputChannel(
	const string& hwname,
	BERT* bert,
	const string& color,
	size_t index)
	: OscilloscopeChannel(nullptr, hwname, color, Unit(Unit::UNIT_FS), index)
	, m_bert(bert)
{
	ClearStreams();

	//Make horizontal bathtub stream
	AddStream(Unit::UNIT_LOG_BER, "HBathtub", Stream::STREAM_TYPE_ANALOG);
	SetVoltageRange(15, STREAM_HBATHTUB);
	SetOffset(7.5, STREAM_HBATHTUB);

	//Make eye pattern stream
	AddStream(Unit::UNIT_VOLTS, "Eye", Stream::STREAM_TYPE_EYE);
	SetVoltageRange(1, STREAM_EYE);	//default, will change when data is acquired
	SetOffset(0, STREAM_EYE);

	//Stream for current BER
	AddStream(Unit(Unit::UNIT_LOG_BER), "RealTimeBER", Stream::STREAM_TYPE_ANALOG_SCALAR);

	//TODO: figure out how to handle vertical bathtubs since right now all streams share the same X axis units
	//and we can't do that since we have X axis units in the time domain
}

BERTInputChannel::~BERTInputChannel()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Vertical scaling and stream management

//This section is mostly lifted from the Filter class since we don't have any of these settings in actual hardware

void BERTInputChannel::ClearStreams()
{
	OscilloscopeChannel::ClearStreams();
	m_ranges.clear();
	m_offsets.clear();
}

size_t BERTInputChannel::AddStream(Unit yunit, const string& name, Stream::StreamType stype, uint8_t flags)
{
	m_ranges.push_back(0);
	m_offsets.push_back(0);
	return OscilloscopeChannel::AddStream(yunit, name, stype, flags);
}

float BERTInputChannel::GetVoltageRange(size_t stream)
{
	if(m_ranges[stream] == 0)
	{
		if(GetData(stream) == nullptr)
			return 1;

		//AutoscaleVertical(stream);
	}

	return m_ranges[stream];
}

void BERTInputChannel::SetVoltageRange(float range, size_t stream)
{
	m_ranges[stream] = range;
}

float BERTInputChannel::GetOffset(size_t stream)
{
	if(m_ranges[stream] == 0)
	{
		if(GetData(stream) == nullptr)
			return 0;

		//AutoscaleVertical(stream);
	}

	return m_offsets[stream];
}

void BERTInputChannel::SetOffset(float offset, size_t stream)
{
	m_offsets[stream] = offset;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Flow graph updates

bool BERTInputChannel::ValidateChannel(size_t /*i*/, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	/*
	if(i >= 2)
		return false;

	if(stream.GetType() == Stream::STREAM_TYPE_ANALOG_SCALAR)
		return true;
	*/

	return false;
}


void BERTInputChannel::Refresh(vk::raii::CommandBuffer& /*cmdBuf*/, shared_ptr<QueueHandle> /*queue*/)
{
	/*
	auto voltageSetPointIn = GetInput(0);
	if(voltageSetPointIn)
	{
		if(Unit(Unit::UNIT_VOLTS) == voltageSetPointIn.GetYAxisUnits())
			m_bert->SetPowerVoltage(m_index, voltageSetPointIn.GetScalarValue());
	}

	auto currentSetPointIn = GetInput(1);
	if(currentSetPointIn)
	{
		if(Unit(Unit::UNIT_AMPS) == currentSetPointIn.GetYAxisUnits())
			m_bert->SetPowerCurrent(m_index, currentSetPointIn.GetScalarValue());
	}
	*/
}
