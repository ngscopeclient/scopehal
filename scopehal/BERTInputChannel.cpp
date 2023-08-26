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
	: InstrumentChannel(hwname, color, Unit(Unit::UNIT_COUNTS), index)
	, m_bert(bert)
{
	ClearStreams();
	AddStream(Unit(Unit::UNIT_RATIO_SCI), "RealTimeBER", Stream::STREAM_TYPE_ANALOG_SCALAR);
	/*AddStream(Unit(Unit::UNIT_VOLTS), "VoltageSetPoint", Stream::STREAM_TYPE_ANALOG_SCALAR);
	AddStream(Unit(Unit::UNIT_AMPS), "CurrentMeasured", Stream::STREAM_TYPE_ANALOG_SCALAR);
	AddStream(Unit(Unit::UNIT_AMPS), "CurrentSetPoint", Stream::STREAM_TYPE_ANALOG_SCALAR);

	CreateInput("VoltageSetPoint");
	CreateInput("CurrentSetPoint");*/
}

BERTInputChannel::~BERTInputChannel()
{
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
