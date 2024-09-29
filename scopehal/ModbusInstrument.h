/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2022 Andrew D. Zonenberg and contributors                                                         *
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

#ifndef ModbusInstrument_h
#define ModbusInstrument_h

/**
	@brief Base class for instruments using Modbus communication protocol
 */
class ModbusInstrument 	: public virtual SCPIInstrument
{
public:
	ModbusInstrument(SCPITransport* transport, uint8_t slaveAdress=1);
	virtual ~ModbusInstrument();

	virtual uint16_t ReadRegister(uint16_t address);
	virtual uint16_t WriteRegister(uint16_t address, uint16_t value);
	virtual uint8_t ReadRegisters(uint16_t address, uint8_t* data, uint8_t count);

protected:
	enum ModbusFunction : uint8_t {
		// Reading functions
		ReadDiscreteOutputCoils = 0x01,
		ReadDiscreteInputContacts = 0x02,
		ReadAnalogOutputHoldingRegisters = 0x03,
		ReadAnalogInputRegisters = 0x04,

		// Single write functions
		WriteSingleDiscreteOutputCoil = 0x05,
		WriteSingleAnalogOutputRegister = 0x06,

		// Multiple write functions
		WriteMultipleDiscreteOutputCoils = 0x0F,
		WriteMultipleAnalogOutputHoldingRegisters = 0x10,

		// User defined
		Undefined = 0x00
	};

	// Make sure several request don't collide before we received the corresponding response
	std::recursive_mutex m_modbusMutex;
	
	uint8_t m_slaveAdress;
	uint16_t CalculateCRC(const uint8_t *buff, size_t len);
	void Converse(ModbusFunction function, std::vector<uint8_t>* data);
	void SendCommand(ModbusFunction function, std::vector<uint8_t>* data);
	void ReadResponse(ModbusFunction function, std::vector<uint8_t>* data);
	void PushUint16(std::vector<uint8_t>* data, uint16_t value);
	uint16_t ReadUint16(std::vector<uint8_t>* data, uint8_t index);

};

#endif
