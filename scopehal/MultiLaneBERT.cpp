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
		SetTxDriveStrength(i, 0.2);
		SetTxEnable(i, true);	//TODO: should we default to on or off?
		SetTxPreCursor(i, 0);
		SetTxPostCursor(i, 0);
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
		case PATTERN_PRBS9:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":POLY PRBS9");
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
		case PATTERN_PRBS9:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":POLY PRBS9");
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

bool MultiLaneBERT::GetRxInvert(size_t i)
{
	return m_rxInvert[i - m_rxChannelBase];
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
// TX driver control

bool MultiLaneBERT::GetTxInvert(size_t i)
{
	return m_txInvert[i];
}

void MultiLaneBERT::SetTxInvert(size_t i, bool invert)
{
	if(invert)
		m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":INVERT 1");
	else
		m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":INVERT 0");

	m_txInvert[i] = invert;
}

vector<float> MultiLaneBERT::GetAvailableTxDriveStrengths(size_t /*i*/)
{
	vector<float> ret;
	ret.push_back(0.0);
	ret.push_back(0.1);
	ret.push_back(0.2);
	ret.push_back(0.3);
	ret.push_back(0.4);
	return ret;
}

float MultiLaneBERT::GetTxDriveStrength(size_t i)
{
	return m_txDrive[i];
}

void MultiLaneBERT::SetTxDriveStrength(size_t i, float drive)
{
	m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":SWING " + to_string((int)(drive*1000)));
	m_txDrive[i] = drive;
}

void MultiLaneBERT::SetTxEnable(size_t i, bool enable)
{
	if(enable)
		m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":ENABLE 1");
	else
		m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":ENABLE 0");

	m_txEnable[i] = enable;
}

bool MultiLaneBERT::GetTxEnable(size_t i)
{
	return m_txEnable[i];
}

float MultiLaneBERT::GetTxPreCursor(size_t i)
{
	return m_txPreCursor[i];
}

void MultiLaneBERT::SetTxPreCursor(size_t i, float precursor)
{
	m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PRECURSOR " + to_string((int)(precursor*100)));
	m_txPreCursor[i] = precursor;
}

float MultiLaneBERT::GetTxPostCursor(size_t i)
{
	return m_txPostCursor[i];
}

void MultiLaneBERT::SetTxPostCursor(size_t i, float postcursor)
{
	m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":POSTCURSOR " + to_string((int)(postcursor*100)));
	m_txPostCursor[i] = postcursor;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Data acquisition

bool MultiLaneBERT::AcquireData()
{
	return true;
}
