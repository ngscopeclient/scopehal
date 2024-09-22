/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Implementation of BERTOutputChannel
	@ingroup core
 */

#include "scopehal.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initialize the channel

	@param hwname	Hardware name of the channel
	@param parent	BERT the channel is part of
	@param color	Initial display color of the channel
	@param index	Number of the channel
 */
BERTOutputChannel::BERTOutputChannel(
	const string& hwname,
	BERT* bert,
	const string& color,
	size_t index)
	: InstrumentChannel(bert, hwname, color, Unit(Unit::UNIT_COUNTS), index)
{
	ClearStreams();
	/*AddStream(Unit(Unit::UNIT_VOLTS), "VoltageMeasured", Stream::STREAM_TYPE_ANALOG_SCALAR);
	AddStream(Unit(Unit::UNIT_VOLTS), "VoltageSetPoint", Stream::STREAM_TYPE_ANALOG_SCALAR);
	AddStream(Unit(Unit::UNIT_AMPS), "CurrentMeasured", Stream::STREAM_TYPE_ANALOG_SCALAR);
	AddStream(Unit(Unit::UNIT_AMPS), "CurrentSetPoint", Stream::STREAM_TYPE_ANALOG_SCALAR);

	CreateInput("VoltageSetPoint");
	CreateInput("CurrentSetPoint");*/

	CreateInput("Amplitude");
}

BERTOutputChannel::~BERTOutputChannel()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Flow graph updates

InstrumentChannel::PhysicalConnector BERTOutputChannel::GetPhysicalConnector()
{
	return CONNECTOR_K_DUAL;
}

bool BERTOutputChannel::ValidateChannel(size_t /*i*/, StreamDescriptor stream)
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


void BERTOutputChannel::Refresh(vk::raii::CommandBuffer& /*cmdBuf*/, shared_ptr<QueueHandle> /*queue*/)
{
	/*
	auto voltageSetPointIn = GetInput(0);
	if(voltageSetPointIn)
	{
		if(Unit(Unit::UNIT_VOLTS) == voltageSetPointIn.GetYAxisUnits())
			GetBERT()->SetPowerVoltage(m_index, voltageSetPointIn.GetScalarValue());
	}

	auto currentSetPointIn = GetInput(1);
	if(currentSetPointIn)
	{
		if(Unit(Unit::UNIT_AMPS) == currentSetPointIn.GetYAxisUnits())
			GetBERT()->SetPowerCurrent(m_index, currentSetPointIn.GetScalarValue());
	}
	*/
}
