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
	@brief Implementation of LoadChannel
	@ingroup datamodel
 */

#include "scopehal.h"
#include "LoadChannel.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

LoadChannel::LoadChannel(
	const string& hwname,
	Load* load,
	const string& color,
	size_t index)
	: InstrumentChannel(load, hwname, color, Unit(Unit::UNIT_FS), index)
{
	ClearStreams();
	AddStream(Unit(Unit::UNIT_VOLTS), "VoltageMeasured", Stream::STREAM_TYPE_ANALOG_SCALAR);
	AddStream(Unit(Unit::UNIT_AMPS), "CurrentMeasured", Stream::STREAM_TYPE_ANALOG_SCALAR);
	AddStream(Unit(Unit::UNIT_AMPS), "SetPoint", Stream::STREAM_TYPE_ANALOG_SCALAR);	//TODO: unit can change w/ mode

	CreateInput("SetPoint");
}

LoadChannel::~LoadChannel()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Flow graph updates

bool LoadChannel::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if(i >= 1)
		return false;

	if(stream.GetType() == Stream::STREAM_TYPE_ANALOG_SCALAR)
		return true;

	return false;
}


void LoadChannel::Refresh(vk::raii::CommandBuffer& /*cmdBuf*/, shared_ptr<QueueHandle> /*queue*/)
{
	auto setPointIn = GetInput(0);
	if(setPointIn)
	{
		//Validate that set point has the correct units
		Unit expectedUnit(Unit::UNIT_COUNTS);
		switch(GetLoad()->GetLoadMode(m_index))
		{
			case Load::MODE_CONSTANT_CURRENT:
				expectedUnit = Unit(Unit::UNIT_AMPS);
				break;

			case Load::MODE_CONSTANT_VOLTAGE:
				expectedUnit = Unit(Unit::UNIT_VOLTS);
				break;

			case Load::MODE_CONSTANT_POWER:
				expectedUnit = Unit(Unit::UNIT_WATTS);
				break;

			case Load::MODE_CONSTANT_RESISTANCE:
				expectedUnit = Unit(Unit::UNIT_OHMS);
				break;
		}

		if(expectedUnit == setPointIn.GetYAxisUnits())
			GetLoad()->SetLoadSetPoint(m_index, setPointIn.GetScalarValue());
	}
}
