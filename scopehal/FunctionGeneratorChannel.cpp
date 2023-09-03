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

FunctionGeneratorChannel::FunctionGeneratorChannel(
	const string& hwname,
	const string& color,
	size_t index)
	: InstrumentChannel(hwname, color, Unit(Unit::UNIT_COUNTS), index)
{
	ClearStreams();

	CreateInput("Frequency");
}

FunctionGeneratorChannel::~FunctionGeneratorChannel()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Input processing

InstrumentChannel::PhysicalConnector FunctionGeneratorChannel::GetPhysicalConnector()
{
	return CONNECTOR_BNC;
}

bool FunctionGeneratorChannel::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;
/*
	if(i >= 1)
		return false;

	if(stream.GetType() == Stream::STREAM_TYPE_ANALOG_SCALAR)
		return true;*/

	return false;
}


void FunctionGeneratorChannel::Refresh(vk::raii::CommandBuffer& /*cmdBuf*/, shared_ptr<QueueHandle> /*queue*/)
{
	/*
	auto setPointIn = GetInput(0);
	if(setPointIn)
	{
		//Validate that set point has the correct units
		Unit expectedUnit(Unit::UNIT_COUNTS);
		switch(m_load->GetLoadMode(m_index))
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
			m_load->SetLoadSetPoint(m_index, setPointIn.GetScalarValue());
	}
	*/
}
