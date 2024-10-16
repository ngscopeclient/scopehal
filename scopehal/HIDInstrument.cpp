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

HIDInstrument::HIDInstrument(SCPITransport* transport, uint8_t slaveAdress)
	: SCPIInstrument(transport, false)
{
	m_slaveAdress = slaveAdress;
}

HIDInstrument::~HIDInstrument()
{

}

void HIDInstrument::PushUint16(std::vector<uint8_t>* data, uint16_t value)
{
	data->push_back(reinterpret_cast<uint8_t *>(&value)[1]);
	data->push_back(reinterpret_cast<uint8_t *>(&value)[0]);
}

uint16_t HIDInstrument::ReadUint16(std::vector<uint8_t>* data, uint8_t index)
{
	if(!data || data->size() <= ((size_t)(index+1)))
		return 0;
	return (static_cast<uint16_t>((*data)[index+1]) + (static_cast<uint16_t>((*data)[index]) << 8)); 
}

void HIDInstrument::Converse(uint8_t reportNumber, size_t responseReportSize, std::vector<uint8_t>* sendData, std::vector<uint8_t>* receiveData)
{	
	lock_guard<recursive_mutex> lock(m_modbusMutex);
	SendReport(reportNumber, sendData);
	ReadReport(responseReportSize,receiveData);
}

void HIDInstrument::SendReport(uint8_t reportNumber, std::vector<uint8_t>* data)
{	// Send the HID report contained in the data buffer
	std::vector<uint8_t> buffer;
	buffer.reserve(data->size()+1);
	buffer.push_back(reportNumber);
	buffer.insert(buffer.end(),data->begin(),data->end());
	m_transport->SendRawData(buffer.size(),buffer.begin().base());
}

void HIDInstrument::ReadReport(size_t reportSize, std::vector<uint8_t>* data)
{	// Read a HID report with the provided size into the specified buffer
	data->reserve(reportSize);
	if(!m_transport->ReadRawData(reportSize,data->begin().base()))
	{
		LogError("Could not read HID report.\n");
		return;
	}
}