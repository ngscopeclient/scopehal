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
#include "PowerSupply.h"

PowerSupply::PowerSupply()
{
}

PowerSupply::~PowerSupply()
{
}


unsigned int PowerSupply::GetInstrumentTypes()
{
	return INST_PSU;
}

bool PowerSupply::SupportsSoftStart()
{
	return false;
}

bool PowerSupply::SupportsIndividualOutputSwitching()
{
	return false;
}

bool PowerSupply::SupportsMasterOutputSwitching()
{
	return false;
}

bool PowerSupply::SupportsOvercurrentShutdown()
{
	return false;
}

bool PowerSupply::GetPowerChannelActive(int /*chan*/)
{
	return true;
}

//Configuration
bool PowerSupply::GetPowerOvercurrentShutdownEnabled(int /*chan*/)
{
	return false;
}

void PowerSupply::SetPowerOvercurrentShutdownEnabled(int /*chan*/, bool /*enable*/)
{
}

bool PowerSupply::GetPowerOvercurrentShutdownTripped(int /*chan*/)
{
	return false;
}

void PowerSupply::SetPowerChannelActive(int /*chan*/, bool /*on*/)
{
}

bool PowerSupply::GetMasterPowerEnable()
{
	return true;
}

void PowerSupply::SetMasterPowerEnable(bool /*enable*/)
{
}

//Soft start
bool PowerSupply::IsSoftStartEnabled(int /*chan*/)
{
	return false;
}

void PowerSupply::SetSoftStartEnabled(int /*chan*/, bool /*enable*/)
{
}

/**
	@brief Pulls data from hardware and updates our measurements
 */
bool PowerSupply::AcquireData()
{
	for(size_t i=0; i<m_channels.size(); i++)
	{
		auto pchan = dynamic_cast<PowerSupplyChannel*>(m_channels[i]);
		if(!pchan)
			continue;

		pchan->SetScalarValue(PowerSupplyChannel::STREAM_VOLTAGE_MEASURED, GetPowerVoltageActual(i));
		pchan->SetScalarValue(PowerSupplyChannel::STREAM_VOLTAGE_SET_POINT, GetPowerVoltageNominal(i));
		pchan->SetScalarValue(PowerSupplyChannel::STREAM_CURRENT_MEASURED, GetPowerCurrentActual(i));
		pchan->SetScalarValue(PowerSupplyChannel::STREAM_CURRENT_SET_POINT, GetPowerCurrentNominal(i));
	}

	return true;
}
