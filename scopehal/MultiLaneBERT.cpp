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
#include "MultiLaneBERT.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

MultiLaneBERT::MultiLaneBERT(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
{
	/*
	//Figure out how many channels we have
	int nchans = atoi(m_model.c_str() + strlen("HMC804"));
	for(int i=0; i<nchans; i++)
	{
		m_channels.push_back(
			new PowerSupplyChannel(string("CH") + to_string(i+1), this, "#808080", i));
	}
	*/
}

MultiLaneBERT::~MultiLaneBERT()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Instrument config

string MultiLaneBERT::GetDriverNameInternal()
{
	return "mlbert";
}

uint32_t MultiLaneBERT::GetInstrumentTypesForChannel(size_t /*i*/)
{
	return INST_BERT;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Channel configuration

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Data acquisition

bool MultiLaneBERT::AcquireData()
{
	return true;
}
