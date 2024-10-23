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
	@brief Implementation of HIDInstrument

	@ingroup core
 */

#include "scopehal.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

HIDInstrument::HIDInstrument(SCPITransport* transport)
	: SCPIInstrument(transport, false)
{
}

HIDInstrument::~HIDInstrument()
{

}

size_t HIDInstrument::Converse(
	uint8_t reportNumber,
	size_t responseReportSize,
	vector<uint8_t>* sendData,
	vector<uint8_t>* receiveData)
{
	lock_guard<recursive_mutex> lock(m_hidMutex);
	SendReport(reportNumber, sendData);
	return ReadReport(responseReportSize,receiveData);
}

/**
	@brief Send a HID report

	@param reportNumber	HID report number
	@param data			Data buffer to send
 */
void HIDInstrument::SendReport(uint8_t reportNumber, vector<uint8_t>* data)
{
	// Send the HID report contained in the data buffer
	lock_guard<recursive_mutex> lock(m_hidMutex);
	if(data)
	{
		vector<uint8_t> buffer;
		// This breaks compilation with latest CXX compiler on Windows:
		// error: 'void operator delete(void*, size_t)' called on pointer '<unknown>' with nonzero offset [1, 9223372036854775807] [-Werror=free-nonheap-object]
		// buffer.reserve(data->size()+1);
		buffer.push_back(reportNumber);
		buffer.insert(buffer.end(),data->begin(),data->end());
		m_transport->SendRawData(buffer.size(),buffer.begin().base());
	}
	else
		LogError("SendReport called with null data buffer, ignoring.\n");
}

size_t HIDInstrument::ReadReport(size_t reportSize, vector<uint8_t>* data)
{
	// Read a HID report with the provided size into the specified buffer
	lock_guard<recursive_mutex> lock(m_hidMutex);
	data->resize(reportSize);
	size_t result = m_transport->ReadRawData(reportSize,data->begin().base());
	// Update vector size according to bytes actually read
	data->resize(result);
	if(result == 0)
	{
		LogError("Could not read HID report.\n");
	}
	return result;
}
