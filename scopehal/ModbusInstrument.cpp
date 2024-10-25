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
	@brief Implementation of ModbusInstrument
	@ingroup core
 */

#include "scopehal.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ModbusInstrument::ModbusInstrument(SCPITransport* transport, uint8_t slaveAdress)
	: SCPIInstrument(transport, false)
{
	m_slaveAdress = slaveAdress;
}

ModbusInstrument::~ModbusInstrument()
{

}

void ModbusInstrument::PushUint16(std::vector<uint8_t>* data, uint16_t value)
{
	data->push_back(reinterpret_cast<uint8_t *>(&value)[1]);
	data->push_back(reinterpret_cast<uint8_t *>(&value)[0]);
}

uint16_t ModbusInstrument::ReadUint16(std::vector<uint8_t>* data, uint8_t index)
{
	if(!data || data->size() <= ((size_t)(index+1)))
		return 0;
	return (static_cast<uint16_t>((*data)[index+1]) + (static_cast<uint16_t>((*data)[index]) << 8));
}

void ModbusInstrument::Converse(ModbusFunction function, std::vector<uint8_t>* data)
{
	lock_guard<recursive_mutex> lock(m_modbusMutex);
	SendCommand (function,data);
	ReadResponse(function,data);
}

uint16_t ModbusInstrument::ReadRegister(uint16_t address)
{
	std::vector<uint8_t> data;
	// Adress to read
	PushUint16(&data,address);
	// Number of registers to read (1)
	PushUint16(&data,0x0001);
	Converse(ReadAnalogOutputHoldingRegisters,&data);
	// Response data should be the 2 bytes of the requested register
	if(data.size() < 2)
	{
		LogError("Invalid response length: %zu, expected 2.\n", data.size());
		return 0;
	}
	return ReadUint16(&data,0);
}

uint16_t ModbusInstrument::WriteRegister(uint16_t address, uint16_t value)
{
	std::vector<uint8_t> data;
	// Adress to write
	PushUint16(&data,address);
	// Data to write
	PushUint16(&data,value);
	Converse(WriteSingleAnalogOutputRegister,&data);
	// Response data should be the 4 bytes (2 adress bytes and 2 bytes for the requested register)
	if(data.size() < 4)
	{
		LogError("Invalid response length: %zu, expected 4.\n",data.size());
		return 0;
	}
	return ReadUint16(&data,2);
}

uint8_t ModbusInstrument::ReadRegisters(uint16_t address, std::vector<uint16_t>* result, uint8_t count)
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
	if(data.size() != byteCount)
	{
		LogError("Invalid response length: %zu, expected %d.\n",data.size(),byteCount);
		return 0;
	}
	result->reserve(count);
	for(int i = 0 ; i < count ; i++)
	{
		result->push_back(ReadUint16(&data,2*i));
	}
	return count;
}


uint16_t ModbusInstrument::CalculateCRC(const uint8_t *buff, size_t len)
{
	static const uint16_t wCRCTable[] =
	{
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
		0X4100, 0X81C1, 0X8081, 0X4040
	};

	uint8_t nTemp;
	uint16_t wCRCWord = 0xFFFF;

	while (len--)
	{
		nTemp = *buff++ ^ wCRCWord;
		wCRCWord >>= 8;
		wCRCWord ^= wCRCTable[nTemp];
	}
	return wCRCWord;
}

void ModbusInstrument::SendCommand(ModbusFunction function, std::vector<uint8_t>* data)
{
	// Modbus query frame format is:
	// | 1 byte slave adress | 1 byte command function # | n bytes of data | 2 bytes CRC |
	std::vector<unsigned char> buffer;
	buffer.push_back(m_slaveAdress);
	buffer.push_back(function);
	for(size_t i = 0 ; i < data->size() ; i++)
	{
		buffer.push_back((unsigned char)(*data)[i]);
	}
	uint16_t crc = CalculateCRC(buffer.begin().base(), buffer.size());
	buffer.push_back(reinterpret_cast<const unsigned char *>(&crc)[0]);
	buffer.push_back(reinterpret_cast<const unsigned char *>(&crc)[1]);
	m_transport->SendRawData(buffer.size(),buffer.begin().base());
}

void ModbusInstrument::ReadResponse(ModbusFunction function, std::vector<uint8_t>* data)
{
	// Modbus response frame format is:
	// 1/ If function is <= 0x04 (read functions):
	//    | 1 byte slave adress | 1 byte command function # (0x03) | 1 byte data length (n) | n bytes of data | 2 bytes CRC |
	// 2/ If function is >  0x04 (write functions):
	//    | 1 byte slave adress | 1 byte command function # (0x06) | 2 bytes register adress | 2 bytes register value | 2 bytes CRC |
	// First read adress, and function
	std::vector<unsigned char> buffer(2);
	if(!m_transport->ReadRawData(2,buffer.begin().base()))
	{
		LogError("Could not read Modbus slave adress and function response.\n");
		return;
	}
	if(buffer[1]!=function)
	{
		LogWarning("Wrong Modbus response function #: %d, expected %d.\n",buffer[1],function);
	}
	uint8_t dataLength;
	if(function <= ReadAnalogInputRegisters)
	{
		// Read data length
		if(!m_transport->ReadRawData(1,buffer.begin().base()))
		{
			LogError("Could not read Modbus data length response.\n");
			return;
		}
		dataLength = buffer[0];
	}
	else
	{
		// Data length is fixed (4 bytes)
		dataLength = 4;
	}
	// Read data and CRC
	buffer.reserve(dataLength+2);
	if(!m_transport->ReadRawData(dataLength+2,buffer.begin().base()))
	{
		LogError("Could not read Modbus data and CRC response.\n");
		return;
	}
	if(data)
	{
		// Move data to result vector
		data->clear();
		data->reserve(dataLength);
		for(int i = 0; i < dataLength ; i++)
		{
			data->push_back(buffer[i]);
		}
	}
}
