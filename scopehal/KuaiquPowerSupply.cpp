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
	@brief Declaration of KuaiquPowerSupply
	@ingroup psudrivers
 */

#include "scopehal.h"
#include "KuaiquPowerSupply.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initialize the driver

	@param transport	SCPITransport connected to the instrument
 */
KuaiquPowerSupply::KuaiquPowerSupply(SCPITransport* transport)
	: SCPIDevice(transport, false), SCPIInstrument(transport, false)
{
	// Only one channel on Kuaiqu PSU
	m_channels.push_back(new PowerSupplyChannel("CH1", this, "#008000", 0));
	m_vendor = "Kuaiqu";
	// Read firmware version number
	m_fwVersion = SendSimpleCommand(COMMAND_FIRMWARE).substr(3,6);
	// Model number
	m_model = "Kuaiqu PSU (" + m_fwVersion + ")";
	// For some reason, Kuaiqu PSU needs to be in LOCK state in order to return meaningfull current and voltage values
	SendSimpleCommand(COMMAND_LOCK_ON);
	// Switch off
	SendSimpleCommand(COMMAND_OFF);
	m_on = false;
	// We have no way to read set values
	m_current = 0;
	m_voltage = 0;
}

KuaiquPowerSupply::~KuaiquPowerSupply()
{
	// Unlock PSI front pannel on exit
	SendSimpleCommand(COMMAND_LOCK_OFF);
}

bool KuaiquPowerSupply::SendWriteValueCommand(Command command, double value)
{
	char buffer[16];
	switch (command)
	{
		case COMMAND_WRITE_CURRENT:
		case COMMAND_WRITE_VOLTAGE:
			{
				int intValue = (int)value;
				int fractValue = ((value - intValue)*1000);
				sprintf(buffer,"<0%c%03d%03d000>",command,intValue,fractValue);
			}
			break;
		default:
			LogError("Command %c is not a read value command.\n",command);
			return 0;
	}
	std::string commandString(buffer);
	std::string result = SendCommand(command,commandString);
	bool success = (result.find("OK") != std::string::npos);
	if(!success)
	{
		LogError("Set value failed, returned '%s'.\n",result.c_str());
	}
	return success;
}

double KuaiquPowerSupply::SendReadValueCommand(Command command)
{
	std::string commandString;
	bool readConstantCurrentState;
	switch (command)
	{
		case COMMAND_READ_VOLTAGE:
			readConstantCurrentState = false;
			commandString = "<02000000000>";
			break;
		case COMMAND_READ_CURRENT:
			readConstantCurrentState = true;
			commandString = "<04000000000>";
			break;
		default:
			LogError("Command %c is not a read value command.\n",command);
			return 0;
	}
	std::string result = SendCommand(command,commandString);
	if(result.size()>=11)
	{
		if(readConstantCurrentState)
		{
			m_constantCurrent = result.at(1) == 'C';
		}
		double floatResult;
		int intPart = std::stoi(result.substr(3,3));
		int fractPart = std::stoi(result.substr(6,3));
		floatResult = intPart + (((double)fractPart)/1000);
		return floatResult;
	}
	else
	{
		LogError("Invalid read value return : '%s'\n",result.c_str());
		return 0;
	}
}

std::string KuaiquPowerSupply::SendSimpleCommand(Command command)
{
	std::string commandString;
	switch (command)
	{
		case COMMAND_LOCK_ON:
			commandString = "<09100000000>";
			break;
		case COMMAND_LOCK_OFF:
			commandString = "<09200000000>";
			break;
		case COMMAND_FIRMWARE:
		case COMMAND_ON:
		case COMMAND_OFF:
			commandString = "<0";
			commandString += command;
			commandString += "000000000>";
			break;
		default:
			LogError("Command %c is not a simple command.\n",command);
			return "";
	}
	return SendCommand(command,commandString);
}

std::string KuaiquPowerSupply::SendCommand(Command command, std::string commandString)
{
	std::string result = "";
	bool needReply = (command != COMMAND_ON && command != COMMAND_OFF);
	// Rate limiting
	this_thread::sleep_until(m_nextCommandReady);
	m_nextCommandReady = chrono::system_clock::now() + m_rateLimitingInterval;
	// Lock guard
	lock_guard<recursive_mutex> lock(m_transportMutex);
	m_transport->SendCommand(commandString);
	if(needReply)
	{
		char tmp = ' ';
		while(true)
		{	// Consume response until we find the end delimiter
			if(!m_transport->ReadRawData(1,(unsigned char*)&tmp))
				break;
			result += tmp;
			if(tmp == '>')
				break;
		}
	}
	return result;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device info

///@brief Return the constant driver name strnig "kuaiqu_psu"
string KuaiquPowerSupply::GetDriverNameInternal()
{
	return "kuaiqu_psu";
}

uint32_t KuaiquPowerSupply::GetInstrumentTypesForChannel([[maybe_unused]] size_t i) const
{
	return INST_PSU;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device capabilities

bool KuaiquPowerSupply::SupportsIndividualOutputSwitching()
{
	return true;
}

bool KuaiquPowerSupply::SupportsVoltageCurrentControl(int chan)
{
	return (chan == 0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual hardware interfacing

bool KuaiquPowerSupply::IsPowerConstantCurrent(int chan)
{
	if(chan == 0)
		return m_constantCurrent;
	else
		return false;
}

double KuaiquPowerSupply::GetPowerVoltageActual(int chan)
{
	if(chan != 0)
		return 0;
	return SendReadValueCommand(COMMAND_READ_VOLTAGE);
}

double KuaiquPowerSupply::GetPowerVoltageNominal(int chan)
{
	if(chan != 0)
		return 0;
	return m_voltage;
}

double KuaiquPowerSupply::GetPowerCurrentActual(int chan)
{
	if(chan != 0)
		return 0;
	return SendReadValueCommand(COMMAND_READ_CURRENT);
}

double KuaiquPowerSupply::GetPowerCurrentNominal(int chan)
{
	if(chan != 0)
		return 0;
	return m_current;
}

bool KuaiquPowerSupply::GetPowerChannelActive(int chan)
{
	if(chan != 0)
		return false;
	return m_on;
}

void KuaiquPowerSupply::SetPowerVoltage(int chan, double volts)
{
	if(chan != 0)
		return;
	SendWriteValueCommand(COMMAND_WRITE_VOLTAGE,volts);
	m_voltage = volts;
}

void KuaiquPowerSupply::SetPowerCurrent(int chan, double amps)
{
	if(chan != 0)
		return;
	SendWriteValueCommand(COMMAND_WRITE_CURRENT,amps);
	m_current = amps;
}

void KuaiquPowerSupply::SetPowerChannelActive(int chan, bool on)
{
	if(chan != 0)
		return;
	SendSimpleCommand(on ? COMMAND_ON : COMMAND_OFF);
	m_on = on;
}
