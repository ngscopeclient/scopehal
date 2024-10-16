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
#include "AlientekPowerSupply.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

AlientekPowerSupply::AlientekPowerSupply(SCPITransport* transport)
	: SCPIDevice(transport, false), SCPIInstrument(transport, false), HIDInstrument(transport)
{
	// Only one channel on Ridden PSU
	m_channels.push_back(new PowerSupplyChannel("CH1", this, "#008000", 0));
	m_vendor = "Alientek";

	SendReceiveReport(Function::SYSTEM_INFO);
	SendReceiveReport(Function::DEVICE_INFO);
	SendReceiveReport(Function::BASIC_INFO);
/*	// Read model number
	uint16_t modelNumber = ReadRegister(REGISTER_MODEL);
	m_model = string("RD") + to_string(modelNumber/10) +"-" + to_string(modelNumber%10);
	// Read serial number
	uint16_t seriallNumber = ReadRegister(REGISTER_SERIAL);
	m_serial = to_string(seriallNumber);
	// Read firmware version number
	float firmwareVersion = ((float)ReadRegister(0x03))/100;
	m_fwVersion = to_string(firmwareVersion);
	// Unlock remote control
	WriteRegister(REGISTER_LOCK,0x00); */
}

AlientekPowerSupply::~AlientekPowerSupply()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device info

string AlientekPowerSupply::GetDriverNameInternal()
{
	return "alientek_dp";
}

uint32_t AlientekPowerSupply::GetInstrumentTypesForChannel(size_t /*i*/) const
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
	if(chan == 0)
		//return (ReadRegister(REGISTER_ERROR)==0x02);
		return false;
	else
		return false;
}

double AlientekPowerSupply::GetPowerVoltageActual(int chan)
{
	if(chan != 0)
		return 0;
	return 0;
	//return ((double)ReadRegister(REGISTER_V_OUT))/100;
}

double AlientekPowerSupply::GetPowerVoltageNominal(int chan)
{
	if(chan != 0)
		return 0;
	return 0;
	//return ((double)ReadRegister(REGISTER_V_SET))/100;
}

double AlientekPowerSupply::GetPowerCurrentActual(int chan)
{
	if(chan != 0)
		return 0;

	return 0;
	//return ((double)ReadRegister(REGISTER_I_OUT))/1000;
}

double AlientekPowerSupply::GetPowerCurrentNominal(int chan)
{
	if(chan != 0)
		return 0;
	return 0;
	//return ((double)ReadRegister(REGISTER_I_SET))/1000;
}

bool AlientekPowerSupply::GetPowerChannelActive(int chan)
{
	if(chan != 0)
		return false;
	return false;
	//return (ReadRegister(REGISTER_ON_OFF)==0x0001);
}

void AlientekPowerSupply::SetPowerVoltage(int chan, double /*volts*/)
{
	if(chan != 0)
		return;
	return;
	//WriteRegister(REGISTER_V_SET,(uint16_t)(volts*100));
}

void AlientekPowerSupply::SetPowerCurrent(int chan, double /*amps*/)
{
	if(chan != 0)
		return;
	return;
	//WriteRegister(REGISTER_I_SET,(uint16_t)(amps*1000));
}

void AlientekPowerSupply::SetPowerChannelActive(int chan, bool /*on*/)
{
	if(chan != 0)
		return;
	return;
	//WriteRegister(REGISTER_ON_OFF, (on ? 0x01 : 0x00));
}

/*
uint8_t HIDInstrument::ReadRegisters(uint16_t address, std::vector<uint16_t>* result, uint8_t count)
{
	if(!result)
		return 0;
	uint16_t byteCount = count*2;
	std::vector<uint8_t> data;
	// Adress to read
	PushUint16(&data,address);
	// Number of registers to read (1)
	PushUint16(&data,count);
	Converse(ReadAnalogOutputHoldingRegisters,&data);
	// Response data should be the 2 bytes of the requested register
	if(data.size()!=byteCount)
	{
		LogError("Invalid response length: %llu, expected %d.\n",data.size(),byteCount);
		return 0;
	}
	result->reserve(count);
	for(int i = 0 ; i < count ; i++)
	{
		result->push_back(ReadUint16(&data,2*i));
	}
	return count;
}
*/

void AlientekPowerSupply::SendReceiveReport(Function function, int sequence, std::vector<uint8_t>* data)
{	// Report has the form:
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
	{	// Data length
		sendData.push_back(data->size());
		// Data
		sendData.insert(sendData.end(),data->begin(),data->end());
	}
	else
	{	// Data length = 0
		sendData.push_back(0);
	}

	// CRC
	uint16_t crc = CalculateCRC(sendData.begin().base(), sendData.size());
	sendData.push_back(reinterpret_cast<const uint8_t *>(&crc)[0]);
	sendData.push_back(reinterpret_cast<const uint8_t *>(&crc)[1]);

	std::vector<uint8_t> receiveData;
	// Report response has the form :
	// - deviceAdr
	// - function
	// - sequenceNumber (optionnal)
	// - contentLength
	// - content (= contentLenth bytes)
	// - checksum (2 bytes)
	// Response size position is 3 and 2 bytes have to be added to content length for the checksum
	Converse(0,64,&sendData,&receiveData);

	// Read received data
	// TODO
}



uint16_t AlientekPowerSupply::CalculateCRC(const uint8_t *buff, size_t len)
{
  /*static const uint16_t wCRCTable[] = {
      0X0000, 0XC0C1, 0XC181, 0X0140, 0XC301, 0X03C0, 0X0280, 0XC241, 0XC601,
      0X06C0, 0X0780, 0XC741, 0X0500, 0XC5C1, 0XC481, 0X0440, 0XCC01, 0X0CC0,
      0X0D80, 0XCD41, 0X0F00, 0XCFC1, 0XCE81, 0X0E40, 0X0A00, 0XCAC1, 0XCB81,
      0X0B40, 0XC901, 0X09C0, 0X0880, 0XC841, 0XD801, 0X18C0, 0X1980, 0XD941,
      0X1B00, 0XDBC1, 0XDA81, 0X1A40, 0X1E00, 0XDEC1, 0XDF81, 0X1F40, 0XDD01,
      0X1DC0, 0X1C80, 0XDC41, 0X1400, 0XD4C1, 0XD581, 0X1540, 0XD701, 0X17C0,
      0X1680, 0XD641, 0XD201, 0X12C0, 0X1380, 0XD341, 0X1100, 0XD1C1, 0XD081,
      0X1040, 0XF001, 0X30C0, 0X3180, 0XF141, 0X3300, 0XF3C1, 0XF281, 0X3240,
      0X3600, 0XF6C1, 0XF781, 0X3740, 0XF501, 0X35C0, 0X3480, 0XF441, 0X3C00,
      0XFCC1, 0XFD81, 0X3D40, 0XFF01, 0X3FC0, 0X3E80, 0XFE41, 0XFA01, 0X3AC0,
      0X3B80, 0XFB41, 0X3900, 0XF9C1, 0XF881, 0X3840, 0X2800, 0XE8C1, 0XE981,
      0X2940, 0XEB01, 0X2BC0, 0X2A80, 0XEA41, 0XEE01, 0X2EC0, 0X2F80, 0XEF41,
      0X2D00, 0XEDC1, 0XEC81, 0X2C40, 0XE401, 0X24C0, 0X2580, 0XE541, 0X2700,
      0XE7C1, 0XE681, 0X2640, 0X2200, 0XE2C1, 0XE381, 0X2340, 0XE101, 0X21C0,
      0X2080, 0XE041, 0XA001, 0X60C0, 0X6180, 0XA141, 0X6300, 0XA3C1, 0XA281,
      0X6240, 0X6600, 0XA6C1, 0XA781, 0X6740, 0XA501, 0X65C0, 0X6480, 0XA441,
      0X6C00, 0XACC1, 0XAD81, 0X6D40, 0XAF01, 0X6FC0, 0X6E80, 0XAE41, 0XAA01,
      0X6AC0, 0X6B80, 0XAB41, 0X6900, 0XA9C1, 0XA881, 0X6840, 0X7800, 0XB8C1,
      0XB981, 0X7940, 0XBB01, 0X7BC0, 0X7A80, 0XBA41, 0XBE01, 0X7EC0, 0X7F80,
      0XBF41, 0X7D00, 0XBDC1, 0XBC81, 0X7C40, 0XB401, 0X74C0, 0X7580, 0XB541,
      0X7700, 0XB7C1, 0XB681, 0X7640, 0X7200, 0XB2C1, 0XB381, 0X7340, 0XB101,
      0X71C0, 0X7080, 0XB041, 0X5000, 0X90C1, 0X9181, 0X5140, 0X9301, 0X53C0,
      0X5280, 0X9241, 0X9601, 0X56C0, 0X5780, 0X9741, 0X5500, 0X95C1, 0X9481,
      0X5440, 0X9C01, 0X5CC0, 0X5D80, 0X9D41, 0X5F00, 0X9FC1, 0X9E81, 0X5E40,
      0X5A00, 0X9AC1, 0X9B81, 0X5B40, 0X9901, 0X59C0, 0X5880, 0X9841, 0X8801,
      0X48C0, 0X4980, 0X8941, 0X4B00, 0X8BC1, 0X8A81, 0X4A40, 0X4E00, 0X8EC1,
      0X8F81, 0X4F40, 0X8D01, 0X4DC0, 0X4C80, 0X8C41, 0X4400, 0X84C1, 0X8581,
      0X4540, 0X8701, 0X47C0, 0X4680, 0X8641, 0X8201, 0X42C0, 0X4380, 0X8341,
      0X4100, 0X81C1, 0X8081, 0X4040};

  uint8_t nTemp;
  uint16_t wCRCWord = 0xFFFF;

  while (len--) {
    nTemp = *buff++ ^ wCRCWord;
    wCRCWord >>= 8;
    wCRCWord ^= wCRCTable[nTemp];
  }
  return wCRCWord;*/
  uint16_t crc = 0xFFFF;

  for (size_t i = 0 ; i < len ; i++) 
  {
	uint8_t data = buff[i];
    crc = crc ^ data;
    for (int j = 0; j < 8; j++) 
	{
      uint8_t odd = crc & 0x0001;
      crc = crc >> 1;
      if (odd) 
	  {
        crc = crc ^ 0xA001;
      }
    }
  }

  return crc;
}

