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

void ModbusInstrument::Converse(ModbusFunction function, std::vector<uint8_t>* data)
{
	lock_guard<recursive_mutex> lock(m_modbusMutex);
	SendCommand (function,(*data));
	ReadResponse(function,data);
}

uint16_t ModbusInstrument::ReadRegister(uint16_t address)
{
	std::vector<uint8_t> data;
	// Adress to read
	PushUint16(&data,address,false);
	// Number of registers to read (1)
	PushUint16(&data,0x0001,false);
	Converse(ReadAnalogOutputHoldingRegisters,&data);
	// Response data should be the 2 bytes of the requested register
	if(data.size() < 2)
	{
		LogError("Invalid response length: %zu, expected 2.\n", data.size());
		return 0;
	}
	return ReadUint16(data,0,false);
}

uint16_t ModbusInstrument::WriteRegister(uint16_t address, uint16_t value)
{
	std::vector<uint8_t> data;
	// Adress to write
	PushUint16(&data,address,false);
	// Data to write
	PushUint16(&data,value,false);
	Converse(WriteSingleAnalogOutputRegister,&data);
	// Response data should be the 4 bytes (2 adress bytes and 2 bytes for the requested register)
	if(data.size() < 4)
	{
		LogError("Invalid response length: %zu, expected 4.\n",data.size());
		return 0;
	}
	return ReadUint16(data,2,false);
}

uint8_t ModbusInstrument::ReadRegisters(uint16_t address, std::vector<uint16_t>* result, uint8_t count)
{
	if(!result)
		return 0;
	uint16_t byteCount = count*2;
	std::vector<uint8_t> data;
	// Adress to read
	PushUint16(&data,address,false);
	// Number of registers to read (1)
	PushUint16(&data,count,false);
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
		result->push_back(ReadUint16(data,2*i,false));
	}
	return count;
}

void ModbusInstrument::SendCommand(ModbusFunction function, const std::vector<uint8_t> &data)
{
	// Modbus query frame format is:
	// | 1 byte slave adress | 1 byte command function # | n bytes of data | 2 bytes CRC |
	std::vector<unsigned char> buffer;
	buffer.push_back(m_slaveAdress);
	buffer.push_back(function);
	for(size_t i = 0 ; i < data.size() ; i++)
	{
		buffer.push_back((unsigned char)data[i]);
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
