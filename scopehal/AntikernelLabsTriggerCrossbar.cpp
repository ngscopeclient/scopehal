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

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

AntikernelLabsTriggerCrossbar::AntikernelLabsTriggerCrossbar(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
{
	//TODO: query data rate from instrument
	//TODO: date rate needs to be a per channel setting, not global: have to extend the API for this
	SetDataRate(10312500000LL);

	//Input-only channels
	m_triggerInChannelBase = m_channels.size();
	for(size_t i=0; i<8; i++)
	{
		m_channels.push_back(new DigitalInputChannel(
			string("IN") + to_string(i + m_triggerInChannelBase),
			"#808080",
			m_channels.size()));
	}

	//Bidir channels
	m_triggerBidirChannelBase = m_channels.size();
	for(size_t i=0; i<4; i++)
	{
		m_channels.push_back(new DigitalIOChannel(
			string("IO") + to_string(i + m_triggerBidirChannelBase),
			"#808080",
			m_channels.size()));
	}

	//Output-only channels
	//TODO: 0-3 are unbuffered, 4-7 are buffered
	//For now we just want to reserve spaces in the channel list
	m_triggerOutChannelBase = m_channels.size();
	for(size_t i=0; i<8; i++)
	{
		m_channels.push_back(new DigitalInputChannel(
			string("OUT") + to_string(i),
			"#808080",
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
			"#808080",
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
	}

	//Set up receiver channels
	m_rxChannelBase = m_channels.size();
	for(int i=0; i<2; i++)
	{
		auto hwname = string("RX") + to_string(i);

		m_channels.push_back(new BERTInputChannel(
			hwname,
			this,
			"#4040c0",
			m_channels.size()));
		/*
		SetRxPattern(i+nchans, PATTERN_PRBS7);
		SetRxInvert(i+nchans, false);
		SetRxCTLEGainStep(i+nchans, 4);
		SetBERSamplingPoint(i+nchans, 0, 0);
		*/
	}

	//Set up default custom pattern
	//SetGlobalCustomPattern(0xff00);

	//Default integration is 10M UIs
	//SetBERIntegrationLength(1e7);
}

AntikernelLabsTriggerCrossbar::~AntikernelLabsTriggerCrossbar()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Instrument config

string AntikernelLabsTriggerCrossbar::GetDriverNameInternal()
{
	return "akl.crossbar";
}

uint32_t AntikernelLabsTriggerCrossbar::GetInstrumentTypesForChannel(size_t i) const
{
	//TODO: trigger types
	if(i < m_txChannelBase)
		return 0;
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

int64_t AntikernelLabsTriggerCrossbar::GetDataRate()
{
	return m_dataRate;
}

void AntikernelLabsTriggerCrossbar::SetDataRate(int64_t rate)
{
	m_transport->SendCommandQueued(string("RATE ") + to_string(rate));
	m_dataRate = rate;
}

vector<int64_t> AntikernelLabsTriggerCrossbar::GetAvailableDataRates()
{
	vector<int64_t> ret;
	ret.push_back(  644531250LL);
	ret.push_back( 1289062500LL);
	ret.push_back( 2578125000LL);
	ret.push_back( 5156250000LL);
	ret.push_back(10312500000LL);
	return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Data acquisition

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

void AntikernelLabsTriggerCrossbar::MeasureHBathtub(size_t i)
{
	auto reply = m_transport->SendCommandQueuedWithReply(m_channels[i]->GetHwname() + ":HBATHTUB?");

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

	//TODO: this is rate dependent, 65 is correct for full-rate but subrate has more
	ssize_t width		= 65;
	ssize_t halfwidth	= (width-1)/2;
	if(values.size() < (size_t)width)
	{
		LogError("not enough data came back (got %zu values expected %zu)\n", values.size(), width);
		return;
	}

	auto rate = GetDataRate();
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
	LogDebug("period = %s\n", Unit(Unit::UNIT_FS).PrettyPrint(period).c_str());

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

	//Lock while we read the lines
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	auto rate = GetDataRate();
	auto period = round(FS_PER_SECOND / rate);

	//For now, expect -32 to +32 (65 values)
	//Sub-rate modes have more
	int32_t height = 64;
	int32_t width = 65;
	int32_t halfwidth = (width-1)/2;
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
		auto reply = m_transport->ReadReply();
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


	/*
	for(int y=0; y<256; y++)
	{
		for(int x=0; x<128; x++)
		{
			//Sample order coming off the BERT is right to left in X axis, then scanning bottom to top in Y
			double ber = values[y*128 + (127-x) + 2];

			//Rescale to generate fake hit count
			//Also need to rearrange so that we get the render-friendly eye pattern scopehal wants
			//(half a UI left and right of the center opening)
			if(x < 64)
				accum[y*256 + x + 192] = ber * 1e15;
			else
				accum[y*256 + x + 64] = ber * 1e15;
		}
	}
	*/
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
