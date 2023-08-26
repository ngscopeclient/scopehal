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
	//Add and provide default configuration for pattern generator channels
	int nchans = 4;
	m_rxChannelBase = nchans;
	for(int i=0; i<nchans; i++)
	{
		m_channels.push_back(new BERTOutputChannel(string("TX") + to_string(i+1), this, "#808080", i));
		SetTxPattern(i, PATTERN_PRBS7);
		SetTxInvert(i, false);
	}

	//Add pattern checker channels
	for(int i=0; i<nchans; i++)
	{
		m_channels.push_back(new BERTInputChannel(string("RX") + to_string(i+1), this, "#808080", i+nchans));
		SetRxPattern(i+nchans, PATTERN_PRBS7);
		SetRxInvert(i+nchans, false);
	}
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

BERT::Pattern MultiLaneBERT::GetTxPattern(size_t i)
{
	return m_txPattern[i];
}

void MultiLaneBERT::SetTxPattern(size_t i, Pattern pattern)
{
	switch(pattern)
	{
		case PATTERN_PRBS7:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":POLY PRBS7");
			break;
		case PATTERN_PRBS15:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":POLY PRBS15");
			break;
		case PATTERN_PRBS23:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":POLY PRBS23");
			break;
		case PATTERN_PRBS31:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":POLY PRBS31");
			break;

		case PATTERN_CUSTOM:
		default:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":POLY CUSTOM");
	}

	m_txPattern[i] = pattern;
}

BERT::Pattern MultiLaneBERT::GetRxPattern(size_t i)
{
	return m_rxPattern[i - m_rxChannelBase];
}

void MultiLaneBERT::SetRxPattern(size_t i, Pattern pattern)
{
	switch(pattern)
	{
		case PATTERN_PRBS7:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":POLY PRBS7");
			break;
		case PATTERN_PRBS15:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":POLY PRBS15");
			break;
		case PATTERN_PRBS23:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":POLY PRBS23");
			break;
		case PATTERN_PRBS31:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":POLY PRBS31");
			break;

		case PATTERN_CUSTOM:
		default:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":POLY AUTO");
	}

	m_rxPattern[i - m_rxChannelBase] = pattern;
}

bool MultiLaneBERT::GetTxInvert(size_t i)
{
	return m_txInvert[i];
}

bool MultiLaneBERT::GetRxInvert(size_t i)
{
	return m_rxInvert[i - m_rxChannelBase];
}

void MultiLaneBERT::SetTxInvert(size_t i, bool invert)
{
	if(invert)
		m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":INVERT 1");
	else
		m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":INVERT 0");

	m_txInvert[i] = invert;
}

void MultiLaneBERT::SetRxInvert(size_t i, bool invert)
{
	if(invert)
		m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":INVERT 1");
	else
		m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":INVERT 0");

	m_rxInvert[i - m_rxChannelBase] = invert;
}

vector<BERT::Pattern> MultiLaneBERT::GetAvailableTxPatterns(size_t /*i*/)
{
	vector<Pattern> ret;
	ret.push_back(PATTERN_PRBS7);
	ret.push_back(PATTERN_PRBS15);
	ret.push_back(PATTERN_PRBS23);
	ret.push_back(PATTERN_PRBS31);
	ret.push_back(PATTERN_CUSTOM);
	return ret;
}

vector<BERT::Pattern> MultiLaneBERT::GetAvailableRxPatterns(size_t /*i*/)
{
	vector<Pattern> ret;
	ret.push_back(PATTERN_PRBS7);
	ret.push_back(PATTERN_PRBS15);
	ret.push_back(PATTERN_PRBS23);
	ret.push_back(PATTERN_PRBS31);
	ret.push_back(PATTERN_AUTO);
	return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Data acquisition

bool MultiLaneBERT::AcquireData()
{
	return true;
}
