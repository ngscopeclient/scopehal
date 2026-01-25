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
	@brief Implementation of RidenPowerSupply
	@ingroup psudrivers
 */

#include "scopehal.h"
#include "RidenPowerSupply.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initialize the driver

	@param transport	SCPITransport connected to the instrument
 */
RidenPowerSupply::RidenPowerSupply(SCPITransport* transport)
	: SCPIDevice(transport, false), SCPIInstrument(transport, false), ModbusInstrument(transport)
{
	// Only one channel on Riden PSU
	m_channels.push_back(new PowerSupplyChannel("CH1", this, "#008000", 0));
	m_vendor = "Riden";
	// Read model number
	uint16_t modelNumber = ReadRegister(REGISTER_MODEL);
	m_model = string("RD") + to_string(modelNumber/10) +"-" + to_string(modelNumber%10);
	// Determine current and voltage factor
	switch(modelNumber)
	{
		case 3005:
		case 5005:
		case 8005:
		case 60061:
		case 60062:
		case 60066:
			m_currentFactor = 1000;
			m_voltageFactor = 100;
			break;
		case 60065:
			m_currentFactor = 10000;
			m_voltageFactor = 100;
			break;
		case 5015:
		case 5020:
		case 60121:
		case 60181:
		case 60241:
			m_currentFactor = 100;
			m_voltageFactor = 100;
			break;
		case 60125:
			m_currentFactor = 1000;
			m_voltageFactor = 1000;
			break;
		default:
			m_currentFactor = 1000;
			m_voltageFactor = 100;
	}
	// Read serial number
	uint16_t serialNumber = ReadRegister(REGISTER_SERIAL);
	m_serial = to_string(serialNumber);
	// Read firmware version number
	float firmwareVersion = ((float)ReadRegister(0x03))/100;
	m_fwVersion = to_string(firmwareVersion);
	// Unlock remote control
	WriteRegister(REGISTER_LOCK,0x00);
}

RidenPowerSupply::~RidenPowerSupply()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device info

///@brief Return the constant driver name "riden_rd"
string RidenPowerSupply::GetDriverNameInternal()
{
	return "riden_rd";
}

uint32_t RidenPowerSupply::GetInstrumentTypesForChannel([[maybe_unused]] size_t i) const
{
	return INST_PSU;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device capabilities

bool RidenPowerSupply::SupportsIndividualOutputSwitching()
{
	return true;
}

bool RidenPowerSupply::SupportsVoltageCurrentControl(int chan)
{
	return (chan == 0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual hardware interfacing

bool RidenPowerSupply::IsPowerConstantCurrent(int chan)
{
	if(chan == 0)
		return (ReadRegister(REGISTER_ERROR)==0x02);
	else
		return false;
}

double RidenPowerSupply::GetPowerVoltageActual(int chan)
{
	if(chan != 0)
		return 0;
	return ((double)ReadRegister(REGISTER_V_OUT))/m_voltageFactor;
}

double RidenPowerSupply::GetPowerVoltageNominal(int chan)
{
	if(chan != 0)
		return 0;
	return ((double)ReadRegister(REGISTER_V_SET))/m_voltageFactor;
}

double RidenPowerSupply::GetPowerCurrentActual(int chan)
{
	if(chan != 0)
		return 0;
	return ((double)ReadRegister(REGISTER_I_OUT))/m_currentFactor;
}

double RidenPowerSupply::GetPowerCurrentNominal(int chan)
{
	if(chan != 0)
		return 0;
	return ((double)ReadRegister(REGISTER_I_SET))/m_currentFactor;
}

bool RidenPowerSupply::GetPowerChannelActive(int chan)
{
	if(chan != 0)
		return false;
	return (ReadRegister(REGISTER_ON_OFF)==0x0001);
}

void RidenPowerSupply::SetPowerVoltage(int chan, double volts)
{
	if(chan != 0)
		return;
	WriteRegister(REGISTER_V_SET,(uint16_t)(volts*m_voltageFactor));
}

void RidenPowerSupply::SetPowerCurrent(int chan, double amps)
{
	if(chan != 0)
		return;
	WriteRegister(REGISTER_I_SET,(uint16_t)(amps*m_currentFactor));
}

void RidenPowerSupply::SetPowerChannelActive(int chan, bool on)
{
	if(chan != 0)
		return;
	WriteRegister(REGISTER_ON_OFF, (on ? 0x01 : 0x00));
}
