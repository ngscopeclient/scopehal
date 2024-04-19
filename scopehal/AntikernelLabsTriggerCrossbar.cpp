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

#include "scopehal.h"
#include "AntikernelLabsTriggerCrossbar.h"
#include "EyeWaveform.h"
#include "BufferedSwitchMatrixOutputChannel.h"
#include "BufferedSwitchMatrixIOChannel.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

AntikernelLabsTriggerCrossbar::AntikernelLabsTriggerCrossbar(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, m_loadInProgress(true)
	, m_bathtubScanInProgress(false)
	, m_eyeScanInProgress(false)
	, m_activeScanChannel(0)
	, m_activeScanProgress(0)
{

}

AntikernelLabsTriggerCrossbar::~AntikernelLabsTriggerCrossbar()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Instrument config

void AntikernelLabsTriggerCrossbar::PostCtorInit()
{
	//Input-only channels
	m_triggerInChannelBase = m_channels.size();
	for(size_t i=0; i<8; i++)
	{
		m_channels.push_back(new DigitalInputChannel(
			string("IN") + to_string(i + m_triggerInChannelBase),
			this,
			"#ffff00",	//yellow
			m_channels.size()));
	}

	//Bidir channels
	m_triggerBidirChannelBase = m_channels.size();
	for(size_t i=0; i<4; i++)
	{
		m_channels.push_back(new BufferedSwitchMatrixIOChannel(
			string("IO") + to_string(i + m_triggerBidirChannelBase),
			this,
			"#ff6abc",	//pink
			m_channels.size()));
	}

	//Output-only channels
	//TODO: 0-3 are unbuffered, 4-7 are buffered, we need to note this somewhere
	//For now we just want to reserve spaces in the channel list
	m_triggerOutChannelBase = m_channels.size();
	for(size_t i=0; i<8; i++)
	{
		m_channels.push_back(new BufferedSwitchMatrixOutputChannel(
			string("OUT") + to_string(i),
			this,
			"#00ffff",	//cyan
			m_channels.size()));
	}

	//TODO: figure out mux config stuff

	//Set up pattern generator channels
	m_txChannelBase = m_channels.size();
	for(int i=0; i<2; i++)
	{
		auto hwname = string("TX") + to_string(i);

		m_channels.push_back(new BERTOutputChannel(
			hwname,
			this,
			"#808080",	//gray
			m_channels.size()));

		//Read existing config once, then cache
		//No need to ever flush cache as instrument has no front panel UI
		auto reply = Trim(m_transport->SendCommandQueuedWithReply(hwname + ":PATTERN?"));
		if(reply == "PRBS7")
			m_txPattern[i] = PATTERN_PRBS7;
		else if(reply == "PRBS15")
			m_txPattern[i] = PATTERN_PRBS15;
		else if(reply == "PRBS23")
			m_txPattern[i] = PATTERN_PRBS23;
		else if(reply == "PRBS31")
			m_txPattern[i] = PATTERN_PRBS31;
		else if(reply == "USER")
			m_txPattern[i] = PATTERN_CUSTOM;
		else if(reply == "FASTSQUARE")
			m_txPattern[i] = PATTERN_CLOCK_DIV2;
		else if(reply == "SLOWSQUARE")
			m_txPattern[i] = PATTERN_CLOCK_DIV32;

		reply = Trim(m_transport->SendCommandQueuedWithReply(hwname + ":INVERT?"));
		m_txInvert[i] = (atoi(reply.c_str()) == 1);

		reply = Trim(m_transport->SendCommandQueuedWithReply(hwname + ":ENABLE?"));
		m_txEnable[i] = (atoi(reply.c_str()) == 1);

		reply = Trim(m_transport->SendCommandQueuedWithReply(hwname + ":SWING?"));
		auto drives = AntikernelLabsTriggerCrossbar::GetAvailableTxDriveStrengths(m_txChannelBase+i);
		size_t idx = atoi(reply.c_str());
		if(idx >= drives.size())
			idx = drives.size() - 1;
		m_txDrive[i] = drives[idx];

		//precursor range is 0 to 20
		reply = Trim(m_transport->SendCommandQueuedWithReply(hwname + ":PRECURSOR?"));
		idx = atoi(reply.c_str());
		m_txPreCursor[i] = idx / 20.0f;

		//postcursor range is 0 to 31
		reply = Trim(m_transport->SendCommandQueuedWithReply(hwname + ":POSTCURSOR?"));
		idx = atoi(reply.c_str());
		m_txPostCursor[i] = idx / 31.0f;

		//Read data rate. This is two different fields for clock divider and PLL source
		//In the current gateware, QPLL is always 10.31235 Gbps and CPLL is always 5 Gbps
		//then we may sub-rate from that
		int64_t pllLineRate = 10312500000LL;
		reply = Trim(m_transport->SendCommandQueuedWithReply(hwname + ":CLKSEL?"));
		if(reply == "CPLL")
			pllLineRate = 5000000000LL;
		reply = Trim(m_transport->SendCommandQueuedWithReply(hwname + ":CLKDIV?"));
		auto clkdiv = stoi(reply);
		if(clkdiv <= 0)		//0 means use OUT_DIV attribute, which is also set to 1 in the gateware
			clkdiv = 1;
		int64_t realClkDiv = 1 << (clkdiv - 1);
		m_txDataRate[i] = pllLineRate / realClkDiv;
	}

	//Set up receiver channels
	m_rxChannelBase = m_channels.size();
	for(int i=0; i<2; i++)
	{
		auto hwname = string("RX") + to_string(i);

		m_channels.push_back(new BERTInputChannel(
			hwname,
			weak_from_this(),
			"#4040c0",	//blue-purple
			m_channels.size()));

		//BER prescaler
		auto reply = Trim(m_transport->SendCommandQueuedWithReply(
			m_channels[m_rxChannelBase + i]->GetHwname() + ":PRESCALE?"));
		m_scanDepth[i] = 1 << (17 + atoi(reply.c_str()));

		//Inversion
		reply = Trim(m_transport->SendCommandQueuedWithReply(hwname + ":INVERT?"));
		m_rxInvert[i] = (atoi(reply.c_str()) == 1);

		//Same as for TX, can we abstract this better?
		int64_t pllLineRate = 10312500000LL;
		reply = Trim(m_transport->SendCommandQueuedWithReply(hwname + ":CLKSEL?"));
		if(reply == "CPLL")
			pllLineRate = 5000000000LL;
		reply = Trim(m_transport->SendCommandQueuedWithReply(hwname + ":CLKDIV?"));
		auto clkdiv = stoi(reply);
		if(clkdiv <= 0)		//0 means use OUT_DIV attribute, which is also set to 1 in the gateware
			clkdiv = 1;
		m_rxClkDiv[i] = clkdiv;
		int64_t realClkDiv = 1 << (clkdiv - 1);
		m_rxDataRate[i] = pllLineRate / realClkDiv;

		/*
		SetRxPattern(i+nchans, PATTERN_PRBS7);
		SetRxCTLEGainStep(i+nchans, 4);
		SetBERSamplingPoint(i+nchans, 0, 0);
		*/
	}

	//Set up default custom pattern
	//SetGlobalCustomPattern(0xff00);

	//Default integration is 10M UIs
	//SetBERIntegrationLength(1e7);

	//Load existing mux config for output ports
	for(int i=0; i<8; i++)
	{
		//Get the existing mux selector
		auto hwname = string("OUT") + to_string(i);
		auto reply = Trim(m_transport->SendCommandQueuedWithReply(hwname + ":MUX?"));
		auto muxsel = stoi(reply);

		//Set up the path
		m_channels[m_triggerOutChannelBase + i]->SetInput(
			0, StreamDescriptor(m_channels[m_triggerInChannelBase + muxsel]));
	}

	//Load existing mux config for bidir ports
	for(int i=0; i<4; i++)
	{
		//Get the direction
		auto hwname = string("IO") + to_string(i + 8);
		auto reply = Trim(m_transport->SendCommandQueuedWithReply(hwname + ":DIR?"));
		if(reply == "IN")
			continue;

		//Get the existing mux selector
		reply = Trim(m_transport->SendCommandQueuedWithReply(hwname + ":MUX?"));
		auto muxsel = stoi(reply);

		//Set up the path
		m_channels[m_triggerBidirChannelBase + i]->SetInput(
			0, StreamDescriptor(m_channels[m_triggerInChannelBase + muxsel]));
	}

	m_loadInProgress = false;
}

void AntikernelLabsTriggerCrossbar::SetMuxPath(size_t dstchan, size_t srcchan)
{
	LogTrace("SetMuxPathOpen %zu %zu\n", dstchan, srcchan);

	if(m_loadInProgress)
		return;

	m_transport->SendCommandQueued(
		m_channels[dstchan]->GetHwname() + ":MUX " + to_string(srcchan - m_triggerInChannelBase));

	//If the destination channel is a bidirectional port, make it an output
	if( (dstchan >= m_triggerBidirChannelBase) && (dstchan < m_triggerOutChannelBase) )
		m_transport->SendCommandQueued(m_channels[dstchan]->GetHwname() + ":DIR OUT");
}

void AntikernelLabsTriggerCrossbar::SetMuxPathOpen(size_t dstchan)
{
	LogTrace("SetMuxPathOpen %zu\n", dstchan);

	if( (dstchan >= m_triggerBidirChannelBase) && (dstchan < m_triggerOutChannelBase) )
		m_transport->SendCommandQueued(m_channels[dstchan]->GetHwname() + ":DIR IN");
}

string AntikernelLabsTriggerCrossbar::GetDriverNameInternal()
{
	return "akl.crossbar";
}

unsigned int AntikernelLabsTriggerCrossbar::GetInstrumentTypes() const
{
	return INST_SWITCH_MATRIX | INST_BERT;
}

uint32_t AntikernelLabsTriggerCrossbar::GetInstrumentTypesForChannel(size_t i) const
{
	if(i < m_txChannelBase)
		return INST_SWITCH_MATRIX;
	else
		return INST_BERT;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// RX pattern checker control

BERT::Pattern AntikernelLabsTriggerCrossbar::GetRxPattern(size_t i)
{
	//return m_rxPattern[i - m_rxChannelBase];
	return PATTERN_PRBS7;
}

void AntikernelLabsTriggerCrossbar::SetRxPattern(size_t i, Pattern pattern)
{
	/*switch(pattern)
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

	m_rxPattern[i - m_rxChannelBase] = pattern;*/
}

vector<BERT::Pattern> AntikernelLabsTriggerCrossbar::GetAvailableRxPatterns(size_t /*i*/)
{
	vector<Pattern> ret;
	ret.push_back(PATTERN_PRBS7);
	ret.push_back(PATTERN_PRBS15);
	ret.push_back(PATTERN_PRBS23);
	ret.push_back(PATTERN_PRBS31);
	return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// RX input buffer control

bool AntikernelLabsTriggerCrossbar::GetRxInvert(size_t i)
{
	//return m_rxInvert[i - m_rxChannelBase];
	return false;
}

void AntikernelLabsTriggerCrossbar::SetRxInvert(size_t i, bool invert)
{
	/*if(invert)
		m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":INVERT 1");
	else
		m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":INVERT 0");

	m_rxInvert[i - m_rxChannelBase] = invert;*/
}

bool AntikernelLabsTriggerCrossbar::HasRxCTLE()
{
	return false;
}

vector<float> AntikernelLabsTriggerCrossbar::GetRxCTLEGainSteps()
{
	vector<float> ret;
	/*ret.push_back(0.67);
	ret.push_back(1.34);
	ret.push_back(2.01);
	ret.push_back(2.68);
	ret.push_back(3.35);
	ret.push_back(4.02);
	ret.push_back(4.69);
	ret.push_back(5.36);
	ret.push_back(6.03);
	ret.push_back(6.7);
	ret.push_back(7.37);
	ret.push_back(8.04);
	ret.push_back(8.71);
	ret.push_back(9.38);
	ret.push_back(10);*/
	return ret;
}

size_t AntikernelLabsTriggerCrossbar::GetRxCTLEGainStep(size_t i)
{
	//return m_rxCtleGainSteps[i - m_rxChannelBase];
	return 0;
}

void AntikernelLabsTriggerCrossbar::SetRxCTLEGainStep(size_t i, size_t step)
{
	//m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":CTLESTEP " + to_string(step));
	//m_rxCtleGainSteps[i - m_rxChannelBase] = step;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TX pattern generator control

vector<BERT::Pattern> AntikernelLabsTriggerCrossbar::GetAvailableTxPatterns(size_t /*i*/)
{
	vector<Pattern> ret;
	ret.push_back(PATTERN_PRBS7);
	ret.push_back(PATTERN_PRBS15);
	ret.push_back(PATTERN_PRBS23);
	ret.push_back(PATTERN_PRBS31);
	ret.push_back(PATTERN_CLOCK_DIV2);
	ret.push_back(PATTERN_CLOCK_DIV32);
	//ret.push_back(PATTERN_CUSTOM);
	return ret;
}

BERT::Pattern AntikernelLabsTriggerCrossbar::GetTxPattern(size_t i)
{
	return m_txPattern[i - m_txChannelBase];
}

void AntikernelLabsTriggerCrossbar::SetTxPattern(size_t i, Pattern pattern)
{
	switch(pattern)
	{
		case PATTERN_PRBS7:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PATTERN PRBS7");
			break;
		case PATTERN_PRBS15:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PATTERN PRBS15");
			break;
		case PATTERN_PRBS23:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PATTERN PRBS23");
			break;
		case PATTERN_PRBS31:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PATTERN PRBS31");
			break;
		case PATTERN_CLOCK_DIV2:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PATTERN FASTSQUARE");
			break;
		case PATTERN_CLOCK_DIV32:
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PATTERN SLOWSQUARE");
			break;

		case PATTERN_CUSTOM:
		default:
			//m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":POLY USER");
			break;
	}

	m_txPattern[i - m_txChannelBase] = pattern;
}

bool AntikernelLabsTriggerCrossbar::IsCustomPatternPerChannel()
{
	return true;
}

size_t AntikernelLabsTriggerCrossbar::GetCustomPatternLength()
{
	return 64;
}

void AntikernelLabsTriggerCrossbar::SetGlobalCustomPattern([[maybe_unused]] uint64_t pattern)
{
}

uint64_t AntikernelLabsTriggerCrossbar::GetGlobalCustomPattern()
{
	return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TX driver control

bool AntikernelLabsTriggerCrossbar::GetTxInvert(size_t i)
{
	return m_txInvert[i - m_txChannelBase];
}

void AntikernelLabsTriggerCrossbar::SetTxInvert(size_t i, bool invert)
{
	if(invert)
		m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":INVERT 1");
	else
		m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":INVERT 0");

	m_txInvert[i - m_txChannelBase] = invert;
}

vector<float> AntikernelLabsTriggerCrossbar::GetAvailableTxDriveStrengths([[maybe_unused]] size_t i)
{
	vector<float> ret;
	ret.push_back(0.269);
	ret.push_back(0.336);
	ret.push_back(0.407);
	ret.push_back(0.474);
	ret.push_back(0.543);
	ret.push_back(0.609);
	ret.push_back(0.677);
	ret.push_back(0.741);
	ret.push_back(0.807);
	ret.push_back(0.866);
	ret.push_back(0.924);
	ret.push_back(0.973);
	ret.push_back(1.018);
	ret.push_back(1.056);
	ret.push_back(1.092);
	ret.push_back(1.119);
	return ret;
}

float AntikernelLabsTriggerCrossbar::GetTxDriveStrength(size_t i)
{
	return m_txDrive[i - m_txChannelBase];
}

void AntikernelLabsTriggerCrossbar::SetTxDriveStrength(size_t i, float drive)
{
	//Convert specified drive strength to a TXDIFFCTRL step value
	auto drives = GetAvailableTxDriveStrengths(i);
	size_t swing = 0;
	for(size_t j=0; j<drives.size(); j++)
	{
		if(drive >= (drives[j] - 0.001))
			swing = j;
	}

	m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":SWING " + to_string(swing));
	m_txDrive[i - m_txChannelBase] = drive;
}

void AntikernelLabsTriggerCrossbar::SetTxEnable(size_t i, bool enable)
{
	if(enable)
		m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":ENABLE 1");
	else
		m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":ENABLE 0");

	m_txEnable[i - m_txChannelBase] = enable;
}

bool AntikernelLabsTriggerCrossbar::GetTxEnable(size_t i)
{
	return m_txEnable[i - m_txChannelBase];
}

float AntikernelLabsTriggerCrossbar::GetTxPreCursor(size_t i)
{
	return m_txPreCursor[i - m_txChannelBase];
}

void AntikernelLabsTriggerCrossbar::SetTxPreCursor(size_t i, float precursor)
{
	//precursor values range from 0 to 20
	int precursorScaled = round(precursor * 20);

	m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PRECURSOR " + to_string(precursorScaled));
	m_txPreCursor[i - m_txChannelBase] = precursor;
}

float AntikernelLabsTriggerCrossbar::GetTxPostCursor(size_t i)
{
	return m_txPostCursor[i - m_txChannelBase];
}

void AntikernelLabsTriggerCrossbar::SetTxPostCursor(size_t i, float postcursor)
{
	//postcursor values range from 0 to 31
	int postcursorScaled = round(postcursor * 31);

	m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":POSTCURSOR " + to_string(postcursorScaled));
	m_txPostCursor[i - m_txChannelBase] = postcursor;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Reference clock output: not present

bool AntikernelLabsTriggerCrossbar::HasRefclkIn()
{
	return false;
}

bool AntikernelLabsTriggerCrossbar::HasRefclkOut()
{
	return false;
}

size_t AntikernelLabsTriggerCrossbar::GetRefclkOutMux()
{
	return 0;
}

void AntikernelLabsTriggerCrossbar::SetRefclkOutMux([[maybe_unused]] size_t i)
{
}

vector<string> AntikernelLabsTriggerCrossbar::GetRefclkOutMuxNames()
{
	vector<string> ret;
	return ret;
}

int64_t AntikernelLabsTriggerCrossbar::GetRefclkOutFrequency()
{
	return 0;
}

int64_t AntikernelLabsTriggerCrossbar::GetRefclkInFrequency()
{
	return 1;
}

void AntikernelLabsTriggerCrossbar::SetUseExternalRefclk([[maybe_unused]] bool external)
{
}

bool AntikernelLabsTriggerCrossbar::GetUseExternalRefclk()
{
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Timebase

void AntikernelLabsTriggerCrossbar::SetBERSamplingPoint(size_t i, int64_t dx, float dy)
{
	/*
	//Offset our X sample point by 0.5 UI since the scopehal convention is midpoint referenced
	float uiWidth = FS_PER_SECOND / m_dataRate;
	float dx_offset = dx + uiWidth/2;

	//Offset our Y sample point by 200 mV (seems to be fixed scale)
	float dy_offset = dy + 0.2;

	m_transport->SendCommandQueued(
		m_channels[i]->GetHwname() +
		":SAMPLE " + to_string(dx_offset * 1e-3) + ", " +	//convert fs to ps
		to_string(dy_offset * 1e3));						//convert v to mv

	m_sampleX[i - m_rxChannelBase] = dx;
	m_sampleY[i - m_rxChannelBase] = dy;
	*/
}

void AntikernelLabsTriggerCrossbar::GetBERSamplingPoint(size_t i, int64_t& dx, float& dy)
{
	//dx = m_sampleX[i - m_rxChannelBase];
	//dy = m_sampleY[i - m_rxChannelBase];
	dx = 0;
	dy = 0;
}

bool AntikernelLabsTriggerCrossbar::IsDataRatePerChannel()
{
	return true;
}

int64_t AntikernelLabsTriggerCrossbar::GetDataRate(size_t i)
{
	if(i >= m_rxChannelBase)
		return m_rxDataRate[i - m_rxChannelBase];
	else if(i >= m_txChannelBase)
		return m_txDataRate[i - m_txChannelBase];

	//not a bert channel
	return 0;
}

void AntikernelLabsTriggerCrossbar::SetDataRate(size_t i, int64_t rate)
{
	//Crack data rate into base clock and divisor
	//Even numbered rows are CPLL, odd are QPLL
	auto rates = GetAvailableDataRates();
	auto it = find(rates.begin(), rates.end(), rate);
	if(it == rates.end())
		return;
	auto nrow = it - rates.begin();
	bool qpll = ((nrow & 1) == 1);

	//but last row is also QPLL
	if(nrow == 8)
		qpll = 1;

	//Lowest numbered rows are highest divsors
	size_t ndiv;
	if(qpll)
	{
		if(nrow == 8)
			ndiv = 1;
		else
			ndiv = 5 - ( (nrow - 1) / 2);
	}
	else
		ndiv = 4 - (nrow/2);

	//TODO: don't change clock source if we're only changing the divisor?

	if(qpll)
		m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":CLKSEL QPLL");
	else
		m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":CLKSEL CPLL");
	m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":CLKDIV " + to_string(ndiv));

	//Update cache
	if(i >= m_rxChannelBase)
	{
		m_rxDataRate[i - m_rxChannelBase] = rate;
		m_rxClkDiv[i - m_rxChannelBase] = ndiv;
	}
	else if(i >= m_txChannelBase)
		m_txDataRate[i - m_txChannelBase] = rate;
}

vector<int64_t> AntikernelLabsTriggerCrossbar::GetAvailableDataRates()
{
	vector<int64_t> ret;

	ret.push_back(  625000000LL);	//CPLL / 8
	ret.push_back(  644531250LL);	//QPLL / 16
	ret.push_back( 1250000000LL);	//CPLL / 4
	ret.push_back( 1289062500LL);	//QPLL / 8
	ret.push_back( 2500000000LL);	//CPLL / 2
	ret.push_back( 2578125000LL);	//QPLL / 4
	ret.push_back( 5000000000LL);	//CPLL
	ret.push_back( 5156250000LL);	//QPLL / 2
	ret.push_back(10312500000LL);	//QPLL
	return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Data acquisition

bool AntikernelLabsTriggerCrossbar::IsEyeScanInProgress(size_t i)
{
	return m_eyeScanInProgress && (i == m_activeScanChannel);
}

bool AntikernelLabsTriggerCrossbar::IsHBathtubScanInProgress(size_t i)
{
	return m_bathtubScanInProgress && (i == m_activeScanChannel);
}

float AntikernelLabsTriggerCrossbar::GetScanProgress(size_t i)
{
	if(i == m_activeScanChannel)
		return m_activeScanProgress;
	else
		return 0;
}

void AntikernelLabsTriggerCrossbar::SetBERIntegrationLength(int64_t uis)
{
	/*m_transport->SendCommandQueued(string("INTEGRATION ") + to_string(uis));
	m_integrationLength = uis;*/
}

int64_t AntikernelLabsTriggerCrossbar::GetBERIntegrationLength()
{
	//return m_integrationLength;
	return 0;
}

bool AntikernelLabsTriggerCrossbar::GetRxCdrLockState(size_t i)
{
	//return m_rxLock[i - m_rxChannelBase];
	return true;
}

bool AntikernelLabsTriggerCrossbar::HasConfigurableScanDepth()
{
	return true;
}

vector<int64_t> AntikernelLabsTriggerCrossbar::GetScanDepths([[maybe_unused]] size_t i)
{
	vector<int64_t> ret;
	int64_t base = 131072;
	for(int j=0; j<12; j++)
		ret.push_back(base * (1 << j));
	return ret;
}

int64_t AntikernelLabsTriggerCrossbar::GetScanDepth(size_t i)
{
	if(i < m_rxChannelBase)
		return 0;
	return m_scanDepth[i - m_rxChannelBase];
}

void AntikernelLabsTriggerCrossbar::SetScanDepth(size_t i, int64_t depth)
{
	if(i < m_rxChannelBase)
		return;

	int logdepth = log2(depth);

	m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PRESCALE " + to_string(logdepth - 17));
	m_scanDepth[i - m_rxChannelBase] = depth;
}

int64_t AntikernelLabsTriggerCrossbar::GetExpectedBathtubCaptureTime(size_t i)
{
	ssize_t halfwidth = GetScanHalfWidth(i);
	ssize_t width = 2*halfwidth + 1;

	//Actual measured bathtub run times at 10.3125 Gbps (full rate so 65 points)
	return width * GetScanDepth(i) * 4 * FS_PER_NANOSECOND;
}

int64_t AntikernelLabsTriggerCrossbar::GetExpectedEyeCaptureTime(size_t i)
{
	//rough estimate, we can probably refine this later
	return 28 * GetExpectedBathtubCaptureTime(i);
}

void AntikernelLabsTriggerCrossbar::MeasureHBathtub(size_t i)
{
	ssize_t halfwidth = GetScanHalfWidth(i);
	ssize_t width = 2*halfwidth + 1;

	//Implement our own (inefficient, but that's fine because we're bottlenecked on data generation anyway)
	//version of ReadReply here to get progress updates
	string reply;
	{
		m_activeScanChannel = i;
		m_activeScanProgress = 0;
		m_bathtubScanInProgress = true;

		lock_guard<recursive_mutex> lock(m_transport->GetMutex());
		m_transport->FlushCommandQueue();
		m_transport->SendCommandImmediate(m_channels[i]->GetHwname() + ":HBATHTUB?");

		//Read the reply
		char tmp = ' ';
		size_t ncommas = 0;
		while(true)
		{
			if(1 != m_transport->ReadRawData(1, (unsigned char*)&tmp))
				break;
			if(tmp == '\n')
				break;
			else
				reply += tmp;

			//update progress every comma
			if(tmp == ',')
			{
				ncommas ++;
				m_activeScanProgress = ncommas * 1.0 / width;
			}
		}

		m_bathtubScanInProgress = false;
	}

	//Parse the reply
	auto data = explode(reply, ',');
	vector<float> values;
	float tmp;
	for(auto num : data)
	{
		sscanf(num.c_str(), "%f", &tmp);

		//clamp negative or zero values so log works
		if(tmp <= 0)
			tmp = 1e-20;

		values.push_back(tmp);
	}

	//Sub-rate modes double the width of the eye for each halving of data rate
	//since PLL step size is constant
	if(values.size() < (size_t)width)
	{
		LogError("not enough data came back (got %zu values expected %zu)\n", values.size(), width);
		return;
	}

	auto rate = GetDataRate(i);
	auto period = round(FS_PER_SECOND / rate);
	auto stepsize = period / width;

	//Create the output waveform
	auto cap = dynamic_cast<UniformAnalogWaveform*>(GetChannel(i)->GetData(BERTInputChannel::STREAM_HBATHTUB));
	if(!cap)
	{
		cap = new UniformAnalogWaveform;
		GetChannel(i)->SetData(cap, BERTInputChannel::STREAM_HBATHTUB);
	}
	cap->PrepareForCpuAccess();
	cap->m_timescale = stepsize;
	cap->m_triggerPhase = -stepsize * halfwidth;
	cap->clear();

	//Copy the samples
	for(ssize_t j=0; j<width; j++)
		cap->m_samples.push_back(log10(values[j]));

	cap->MarkModifiedFromCpu();
}

void AntikernelLabsTriggerCrossbar::MeasureEye(size_t i)
{
	auto chan = dynamic_cast<BERTInputChannel*>(GetChannel(i));
	if(!chan)
		return;

	m_activeScanChannel = i;
	m_activeScanProgress = 0;
	m_eyeScanInProgress = true;

	//Lock while we read the lines
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	auto rate = GetDataRate(i);
	auto period = round(FS_PER_SECOND / rate);

	//Sub-rate modes double the width of the eye for each halving of data rate
	//since PLL step size is constant
	int32_t halfwidth = 16 << m_rxClkDiv[i - m_rxChannelBase];

	//For now, expect -32 to +32 (65 values)
	//Sub-rate modes have more
	int32_t height = 64;
	int32_t width = 2*halfwidth + 1;
	int32_t tqwidth = halfwidth + width;

	//Create the output waveform
	//Make the texture double width due to normalization etc
	auto cap = new EyeWaveform(2*width, height, 0.0, EyeWaveform::EYE_BER);
	cap->m_timescale = period;
	chan->SetData(cap, BERTInputChannel::STREAM_EYE);
	cap->PrepareForCpuAccess();

	//Set up metadata
	//For now, assume full rate height is 1.89 mV per code per Xilinx forum post
	//This is 480 mV for +/- 127 codes (although we step by 4 codes per pixel to speed the scan for now)
	chan->SetVoltageRange(0.48, BERTInputChannel::STREAM_EYE);
	cap->m_uiWidth = period;
	cap->m_saturationLevel = 1;

	//Read and process the eye
	m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":EYESCAN?");
	m_transport->FlushCommandQueue();
	auto accum = cap->GetAccumData();
	for(int y=0; y<height; y++)
	{
		string reply;

		//Implement our own (inefficient, but that's fine because we're bottlenecked on data generation anyway)
		//version of ReadReply here to get progress updates
		char ctmp = ' ';
		size_t ncommas = 0;
		while(true)
		{
			if(1 != m_transport->ReadRawData(1, (unsigned char*)&ctmp))
				break;
			if(ctmp == '\n')
				break;
			else
				reply += ctmp;

			//update progress every comma
			if(ctmp == ',')
			{
				ncommas ++;
				float rowProgress = ncommas * 1.0 / width;
				m_activeScanProgress = (y + rowProgress) / height;
			}
		}

		auto data = explode(reply, ',');
		vector<float> values;
		float tmp;
		for(auto num : data)
		{
			sscanf(num.c_str(), "%f", &tmp);
			values.push_back(tmp);
		}

		//Validate width
		if(values.size() < (size_t)width)
		{
			LogError("not enough data came back\n");
			continue;
		}

		for(int x=0; x<width; x++)
		{
			//Rescale to generate hit count for eye test logic
			//Also need to rearrange so that we get the render-friendly eye pattern scopehal wants
			//(half a UI left and right of the center opening)
			if(x <= halfwidth)
				accum[y*width*2 + x + tqwidth] = values[x] * 1e15;
			else
				accum[y*width*2 + x + halfwidth] = values[x] * 1e15;
		}
	}

	m_eyeScanInProgress = false;

	cap->Normalize();
	cap->IntegrateUIs(1);	//have to put something here, but we don't have the true count value

	//Check against the eye pattern
	/*auto rate = chan->GetMask().CalculateHitRate(
		cap,
		256,
		256,
		vrange,
		256.0 / (2*cap->m_uiWidth),
		-cap->m_uiWidth);
	GetChannel(i)->SetScalarValue(BERTInputChannel::STREAM_MASKHITRATE, rate);
	cap->SetMaskHitRate(rate);*/

	cap->MarkModifiedFromCpu();
}

bool AntikernelLabsTriggerCrossbar::AcquireData()
{
	/*
	//Poll CDR lock status
	for(int i=0; i<4; i++)
	{
		auto reply = m_transport->SendCommandQueuedWithReply(m_channels[i + m_rxChannelBase]->GetHwname() + ":LOCK?");
		m_rxLock[i] = (reply == "1");
	}

	//Read BER for each channel
	auto sber = m_transport->SendCommandQueuedWithReply("BER?");
	float bers[4];
	sscanf(sber.c_str(), "%f,%f,%f,%f", &bers[0], &bers[1], &bers[2], &bers[3]);

	for(size_t i=0; i<4; i++)
	{
		//For some reason we sometimes report NaN as BER if there's no errors
		if( (isnan(bers[i])) || (bers[i] == 0) )
			bers[i] = -20;
		else
			bers[i] = log10(bers[i]);

		GetChannel(i+m_rxChannelBase)->SetScalarValue(BERTInputChannel::STREAM_BER, bers[i]);
	}
	*/
	return true;
}
