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
	@author Frederic BORRY
	@brief Implementation of AlientekPowerSupply

	@ingroup psudrivers
 */

#include "scopehal.h"
#include "AlientekPowerSupply.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

AlientekPowerSupply::AlientekPowerSupply(SCPITransport* transport)
	: SCPIDevice(transport, false), SCPIInstrument(transport, false), HIDInstrument(transport)
{
	// Only one channel on Alientek PSU
	m_channels.push_back(new PowerSupplyChannel("CH1", this, "#00C100", 0));

	auto hidTransport = dynamic_cast<SCPIHIDTransport*>(transport);
	if(hidTransport)
	{
		m_vendor = hidTransport->GetManufacturerName();
		m_model =  hidTransport->GetProductName();
		m_serial = hidTransport->GetSerialNumber();
	}
	else
	{
		m_vendor = "Alientek";
		m_model = "DP-100";
	}

	SendReceiveReport(Function::SYSTEM_INFO);
	SendReceiveReport(Function::DEVICE_INFO);
	SendReceiveReport(Function::BASIC_INFO);
	SendGetBasicSetReport();
}

AlientekPowerSupply::~AlientekPowerSupply()
{

}

void AlientekPowerSupply::SendGetBasicSetReport()
{
	std::vector<uint8_t> data(1,Operation::READ);
	SendReceiveReport(Function::BASIC_SET,0,&data);
}

void AlientekPowerSupply::SendSetBasicSetReport()
{
	std::vector<uint8_t> data;
	data.push_back(Operation::OUTPUT);
	data.push_back(m_powerState);
	PushUint16(&data,(uint16_t)std::round(m_vOutSet*1000));
	PushUint16(&data,(uint16_t)std::round(m_iOutSet*1000));
	PushUint16(&data,(uint16_t)std::round(m_ovpSet*1000));
	PushUint16(&data,(uint16_t)std::round(m_ocpSet*1000));
	SendReceiveReport(Function::BASIC_SET,0,&data);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device info

///@brief Return the constant driver name "alientek_dp"
string AlientekPowerSupply::GetDriverNameInternal()
{
	return "alientek_dp";
}

uint32_t AlientekPowerSupply::GetInstrumentTypesForChannel([[maybe_unused]] size_t i) const
{
	return INST_PSU;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device capabilities

bool AlientekPowerSupply::SupportsIndividualOutputSwitching()
{
	return true;
}

bool AlientekPowerSupply::SupportsVoltageCurrentControl(int chan)
{
	return (chan == 0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual hardware interfacing

bool AlientekPowerSupply::IsPowerConstantCurrent(int chan)
{
	if(chan != 0)
		return false;
	SendReceiveReport(Function::BASIC_INFO);
	return (m_outMode == 0);
}

double AlientekPowerSupply::GetPowerVoltageActual(int chan)
{
	if(chan != 0)
		return 0;
	SendReceiveReport(Function::BASIC_INFO);
	return m_vOut;
}

double AlientekPowerSupply::GetPowerVoltageNominal(int chan)
{
	if(chan != 0)
		return 0;
	SendGetBasicSetReport();
	return m_vOutSet;
}

double AlientekPowerSupply::GetPowerCurrentActual(int chan)
{
	if(chan != 0)
		return 0;
	SendReceiveReport(Function::BASIC_INFO);
	return m_iOut;
}

double AlientekPowerSupply::GetPowerCurrentNominal(int chan)
{
	if(chan != 0)
		return 0;
	SendGetBasicSetReport();
	return m_iOutSet;
}

bool AlientekPowerSupply::GetPowerChannelActive(int chan)
{
	if(chan != 0)
		return false;
	SendGetBasicSetReport();
	return m_powerState;
}

void AlientekPowerSupply::SetPowerVoltage(int chan, double volts)
{
	if(chan != 0)
		return;
	// Prevent the value from changing during operation
	lock_guard<recursive_mutex> lock(m_hidMutex);
	m_vOutSet = volts;
	SendSetBasicSetReport();
}

void AlientekPowerSupply::SetPowerCurrent(int chan, double amps)
{
	if(chan != 0)
		return;
	// Prevent the value from changing during operation
	lock_guard<recursive_mutex> lock(m_hidMutex);
	m_iOutSet = amps;
	SendSetBasicSetReport();
}

void AlientekPowerSupply::SetPowerChannelActive(int chan, bool on)
{
	if(chan != 0)
		return;
	// Prevent the value from changing during operation
	lock_guard<recursive_mutex> lock(m_hidMutex);
	m_powerState = (uint8_t)on;
	SendSetBasicSetReport();
}

void AlientekPowerSupply::SendReceiveReport(Function function, int sequence, std::vector<uint8_t>* data)
{
	// Check cache
	if(function == Function::BASIC_INFO)
	{
		if(chrono::system_clock::now() < m_nextBasicInfoUpdate)
			return; // Keep current values
		m_nextBasicInfoUpdate = chrono::system_clock::now() + m_basicInfoCacheDuration;
	}
	else if (function == Function::BASIC_SET && (*data)[0] != Operation::OUTPUT)
	{	// No caching for write operation
		if(chrono::system_clock::now() < m_nextBasicSetUpdate)
			return; // Keep current values
		m_nextBasicSetUpdate = chrono::system_clock::now() + m_basicSetCacheDuration;
	}
	// Report has the form:
	// - deviceAdr
	// - function
	// - sequenceNumber (optionnal)
	// - contentLength
	// - content (= contentLenth bytes)
	// - checksum (2 bytes)
	std::vector<uint8_t> sendData;

	// Device address
	sendData.push_back(m_deviceAdress);
	// Function
	sendData.push_back(function);
	// Sequence
	if(sequence >= 0)
		sendData.push_back(sequence);
	else
		sendData.push_back(1);
	if(data && data->size() > 0)
	{
		// Data length
		sendData.push_back(data->size());
		// Data
		sendData.insert(sendData.end(),data->begin(),data->end());
	}
	else
	{
		// Data length = 0
		sendData.push_back(0);
	}

	// CRC
	uint16_t crc = CalculateCRC(sendData.begin().base(), sendData.size());
	PushUint16(&sendData,crc);

#define HEADER_LENGTH	4
	std::vector<uint8_t> receiveData;
	// Report response has the form :
	// - deviceAdr
	// - function
	// - sequenceNumber (optionnal)
	// - contentLength
	// - content (= contentLenth bytes)
	// - checksum (2 bytes)
	// Response size position is 3 and 2 bytes have to be added to content length for the checksum
	// Maximum report size is around 40 bytes + crc and header => 64 bytes are enough
	size_t bytesRead = Converse(0,64,sendData,&receiveData);
	if(bytesRead < HEADER_LENGTH+1)
	{
		LogError("Invalid report length %zu: missing data.\n",bytesRead);
		return;
	}
	// Read received data
	//uint8_t deviceAdress  = receiveData[0];
	uint8_t reportFunction  = receiveData[1];
	//uint8_t sequence      = receiveData[2];
	//uint8_t contentLength = receiveData[3];
	switch(reportFunction)
	{
		case Function::BASIC_INFO:
			if(bytesRead < HEADER_LENGTH+15)
			{
				LogError("Invalid BasicInfo report length: %zu.\n",bytesRead);
				return;
			}
			m_vIn		= ((double)ReadUint16(receiveData,HEADER_LENGTH+0))/1000;
			m_vOut 		= ((double)ReadUint16(receiveData,HEADER_LENGTH+2))/1000;
			m_iOut 		= ((double)ReadUint16(receiveData,HEADER_LENGTH+4))/1000;
			m_vOutMax 	= ((double)ReadUint16(receiveData,HEADER_LENGTH+6))/1000;
			m_temp1 	= ((double)ReadUint16(receiveData,HEADER_LENGTH+8))/10;
			m_temp2 	= ((double)ReadUint16(receiveData,HEADER_LENGTH+10))/10;
			m_dc5V		= ((double)ReadUint16(receiveData,HEADER_LENGTH+12))/1000;
			m_outMode 	= ReadUint8(receiveData,HEADER_LENGTH+14);
			m_workState = ReadUint8(receiveData,HEADER_LENGTH+15);
			break;
		case Function::BASIC_SET:
			if(bytesRead >= HEADER_LENGTH && receiveData[HEADER_LENGTH-1] == 1)
				return; // This is a response to a write message, ignore it
			if(bytesRead < HEADER_LENGTH+9)
			{
				LogError("Invalid BasicSettings report length: %zu.\n",bytesRead);
				return;
			}
			{
				//uint8_t ack 	= ReadUint8(receiveData,HEADER_LENGTH+0);
				m_powerState	= (bool)ReadUint8(receiveData,HEADER_LENGTH+1);
				m_vOutSet 		= ((double)ReadUint16(receiveData,HEADER_LENGTH+2))/1000;
				m_iOutSet	 	= ((double)ReadUint16(receiveData,HEADER_LENGTH+4))/1000;
				m_ovpSet		= ((double)ReadUint16(receiveData,HEADER_LENGTH+6))/1000;
				m_ocpSet		= ((double)ReadUint16(receiveData,HEADER_LENGTH+8))/1000;
			}
			break;
		case Function::SYSTEM_INFO:
			if(bytesRead < HEADER_LENGTH+7)
			{
				LogError("Invalid SystemInfo report length: %zu.\n",bytesRead);
				return;
			}
			{
				double otp 			= ((double)ReadUint16(receiveData,HEADER_LENGTH+0));
				double opp 			= ((double)ReadUint16(receiveData,HEADER_LENGTH+2))/10;
				uint8_t backlight	= ReadUint8(receiveData,HEADER_LENGTH+4);
				uint8_t volume		= ReadUint8(receiveData,HEADER_LENGTH+5);
				uint8_t revProt		= ReadUint8(receiveData,HEADER_LENGTH+6);
				uint8_t audioOut	= ReadUint8(receiveData,HEADER_LENGTH+7);
				LogDebug("SysInfo: otp = %f, opp= %f, backlight = %d, volume = %d, revProt=%d, audio=%d\n",otp,opp,backlight,volume,revProt,audioOut);
			}
			break;
		case Function::DEVICE_INFO:
			if(bytesRead < HEADER_LENGTH+39)
			{
				LogError("Invalid DeviceInfo report length: %zu.\n",bytesRead);
				return;
			}
			{
				std::string deviceName(receiveData.begin()+HEADER_LENGTH+0,receiveData.begin()+HEADER_LENGTH+15);
				double hardwareVersion = ((double)ReadUint16(receiveData,HEADER_LENGTH+16))/10;
				double firmwareVersion = ((double)ReadUint16(receiveData,HEADER_LENGTH+18))/10;
				uint16_t bootVersion = ((double)ReadUint16(receiveData,HEADER_LENGTH+20))/10;
				uint16_t runVersion  = ((double)ReadUint16(receiveData,HEADER_LENGTH+22))/10;
				std::string serialNumber(receiveData.begin()+HEADER_LENGTH+24,receiveData.begin()+HEADER_LENGTH+24+11);
				uint16_t year		= ReadUint16(receiveData,HEADER_LENGTH+36);
				uint8_t month		= ReadUint8(receiveData,HEADER_LENGTH+38);
				uint8_t day			= ReadUint8(receiveData,HEADER_LENGTH+39);
				LogDebug("DeviceInfo: name = %s, hwVer = %f, fwVer= %f, bootVer = %d, runVer = %d, serial=%s, %d/%d/%d\n",
				deviceName.c_str(),hardwareVersion,firmwareVersion,bootVersion,runVersion,serialNumber.c_str(),year,month,day);
			}
			break;
		default:
			LogWarning("Unsuportd function %x\n",reportFunction);
			break;

	}
}