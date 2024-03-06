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
	/*
	//Change the data rate
	SetUseExternalRefclk(false);
	SetDataRate(10312500000LL);
	*/

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

	//Add and provide default configuration for pattern generator channels
	m_txChannelBase = m_channels.size();
	for(int i=0; i<2; i++)
	{
		m_channels.push_back(new BERTOutputChannel(
			string("TX") + to_string(i),
			this,
			"#808080",
			i));
		/*SetTxPattern(i, PATTERN_PRBS7);
		SetTxInvert(i, false);*/
		SetTxDriveStrength(i, 0.269);
		/*SetTxEnable(i, true);
		SetTxPreCursor(i, 0);
		SetTxPostCursor(i, 0);*/
	}
	/*
	//Add pattern checker channels
	for(int i=0; i<nchans; i++)
	{
		m_channels.push_back(new BERTInputChannel(string("RX") + to_string(i+1), this, "#4040c0", i+nchans));
		SetRxPattern(i+nchans, PATTERN_PRBS7);
		SetRxInvert(i+nchans, false);
		SetRxCTLEGainStep(i+nchans, 4);
		SetBERSamplingPoint(i+nchans, 0, 0);
	}

	//Apply the deferred changes
	//This results in a single API call instead of four for each channel, causing a massive speedup during initialization
	transport->SendCommandQueued("APPLY");

	//Set up default custom pattern
	SetGlobalCustomPattern(0xff00);

	//Set the output mux refclk to LO/32
	SetRefclkOutMux(LO_DIV32_OR_80);

	//Default integration is 10M UIs
	SetBERIntegrationLength(1e7);
	*/
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

uint32_t AntikernelLabsTriggerCrossbar::GetInstrumentTypesForChannel(size_t /*i*/) const
{
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
	//ret.push_back(PATTERN_CUSTOM);
	return ret;
}

BERT::Pattern AntikernelLabsTriggerCrossbar::GetTxPattern(size_t i)
{
	//return m_txPattern[i];
	return PATTERN_PRBS7;
}

void AntikernelLabsTriggerCrossbar::SetTxPattern(size_t i, Pattern pattern)
{
	/*
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
			m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":POLY USER");
	}

	m_txPattern[i] = pattern;
	*/
}

bool AntikernelLabsTriggerCrossbar::IsCustomPatternPerChannel()
{
	return true;
}

size_t AntikernelLabsTriggerCrossbar::GetCustomPatternLength()
{
	return 16;
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
	return false;
	//return m_txInvert[i];
}

void AntikernelLabsTriggerCrossbar::SetTxInvert(size_t i, bool invert)
{
	/*if(invert)
		m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":INVERT 1");
	else
		m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":INVERT 0");

	m_txInvert[i] = invert;*/
}

vector<float> AntikernelLabsTriggerCrossbar::GetAvailableTxDriveStrengths([[maybe_unused]] size_t i)
{
	vector<float> ret;
	/*ret.push_back(0.0);
	ret.push_back(0.1);
	ret.push_back(0.2);
	ret.push_back(0.3);
	ret.push_back(0.4);*/
	return ret;
}

float AntikernelLabsTriggerCrossbar::GetTxDriveStrength(size_t i)
{
	//return m_txDrive[i];
	return 1;
}

void AntikernelLabsTriggerCrossbar::SetTxDriveStrength(size_t i, float drive)
{
	//m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":SWING " + to_string((int)(drive*1000)));
	//m_txDrive[i] = drive;
}

void AntikernelLabsTriggerCrossbar::SetTxEnable(size_t i, bool enable)
{
	/*
	if(enable)
		m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":ENABLE 1");
	else
		m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":ENABLE 0");

	m_txEnable[i] = enable;
	*/
}

bool AntikernelLabsTriggerCrossbar::GetTxEnable(size_t i)
{
	//return m_txEnable[i];
	return true;
}

float AntikernelLabsTriggerCrossbar::GetTxPreCursor(size_t i)
{
	//return m_txPreCursor[i];
	return 1;
}

void AntikernelLabsTriggerCrossbar::SetTxPreCursor(size_t i, float precursor)
{
	//m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":PRECURSOR " + to_string((int)(precursor*100)));
	//m_txPreCursor[i] = precursor;
}

float AntikernelLabsTriggerCrossbar::GetTxPostCursor(size_t i)
{
	//return m_txPostCursor[i];
	return 1;
}

void AntikernelLabsTriggerCrossbar::SetTxPostCursor(size_t i, float postcursor)
{
	//m_transport->SendCommandQueued(m_channels[i]->GetHwname() + ":POSTCURSOR " + to_string((int)(postcursor*100)));
	//m_txPostCursor[i] = postcursor;
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
	//return m_dataRate;
	return 1;
}

void AntikernelLabsTriggerCrossbar::SetDataRate(int64_t rate)
{
	/*
	m_transport->SendCommandQueued(string("RATE ") + to_string(rate));
	m_dataRate = rate;

	//Reset refclk out mux
	SetRefclkOutMux(m_refclkOutMux);
	SetGlobalCustomPattern(m_txCustomPattern);*/
}

vector<int64_t> AntikernelLabsTriggerCrossbar::GetAvailableDataRates()
{
	vector<int64_t> ret;
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
	/*
	auto reply = m_transport->SendCommandQueuedWithReply(m_channels[i]->GetHwname() + ":HBATHTUB?");

	//Parse the reply
	auto data = explode(reply, ',');
	vector<float> values;
	float tmp;
	for(auto num : data)
	{
		sscanf(num.c_str(), "%f", &tmp);
		values.push_back(tmp);
	}

	if(values.size() < 256)
	{
		LogError("not enough data came back\n");
		return;
	}

	//Create the output waveform
	auto cap = dynamic_cast<SparseAnalogWaveform*>(GetChannel(i)->GetData(BERTInputChannel::STREAM_HBATHTUB));
	if(!cap)
	{
		cap = new SparseAnalogWaveform;
		GetChannel(i)->SetData(cap, BERTInputChannel::STREAM_HBATHTUB);
	}
	cap->PrepareForCpuAccess();
	cap->m_timescale = 1;
	cap->clear();*/

	/*
		Format of incoming data (if doing dual Dirac server side)
			Timestamp (in ps relative to start of UI)
			BER (raw, not logarithmic)

		Up to 128 total pairs of points
			Points coming from left side of bathtub
			Dummy with timestamp of zero and BER of zero
			Points coming from right side of bathtub
			Zeroes as filler up to 128
	 */
	 /*
	//int state = 0;
	float last_time = 0;
	for(size_t j=0; j<128; j++)
	{
		float time = values[j*2];
		float ber = values[j*2 + 1];

		//If time goes backwards, we're dealing with a bug in mlBert_CalculateBathtubDualDirac
		//Discard this point
		if(time < last_time)
			continue;
		last_time = time;

		//If BER is invalid, we got hit by the bug also
		if(isinf(ber) || isnan(ber))
			continue;

		//Log doesn't work for zero, so clamp to a very small value
		if(ber < 1e-20)
			ber = 1e-20;

		{
			cap->m_offsets.push_back(round(time * 1000));	//convert ps to fs
			cap->m_samples.push_back(log10(ber));			//convert ber to logarithmic since display units are linear
		}
	}

	//Calculate durations
	for(size_t j=1; j<cap->m_offsets.size(); j++)
		cap->m_durations.push_back(cap->m_offsets[j] - cap->m_offsets[j-1]);
	cap->m_durations.push_back(1);

	//Time-shift entire waveform so zero is at the eye midpoint
	int64_t start = cap->m_offsets[0];
	int64_t end = cap->m_offsets[cap->m_offsets.size() - 1];
	cap->m_triggerPhase = -(start + end) / 2;
	for(size_t j=0; j<cap->m_offsets.size(); j++)
		cap->m_offsets[j] -= start;

	cap->MarkModifiedFromCpu();*/
}

void AntikernelLabsTriggerCrossbar::MeasureEye(size_t i)
{
	/*
	auto reply = m_transport->SendCommandQueuedWithReply(m_channels[i]->GetHwname() + ":EYE?");
	auto chan = dynamic_cast<BERTInputChannel*>(GetChannel(i));
	if(!chan)
		return;

	//Parse the reply
	auto data = explode(reply, ',');
	vector<float> values;
	float tmp;
	for(auto num : data)
	{
		sscanf(num.c_str(), "%f", &tmp);
		values.push_back(tmp);
	}

	if(values.size() < 32770)	//expect 32k plus x and y spacing
	{
		LogError("not enough data came back\n");
		return;
	}

	//Extract offsets
	int64_t dx_fs = round(values[0] * 1e3);
	float dy_v = values[1] * 1e-3;

	//Create the output waveform
	//Always 128 phases x 256 ADC codes and centered at 0V (since the input is AC coupled)
	//Make the texture 256 pixels wide due to normalization etc
	auto cap = new EyeWaveform(256, 256, 0.0, EyeWaveform::EYE_BER);
	cap->m_timescale = dx_fs;
	chan->SetData(cap, BERTInputChannel::STREAM_EYE);
	cap->PrepareForCpuAccess();

	//Set up metadata
	auto vrange = dy_v * 256;
	chan->SetVoltageRange(vrange, BERTInputChannel::STREAM_EYE);
	cap->m_uiWidth = dx_fs * 128;
	cap->m_saturationLevel = 3;

	//Copy the actual data
	auto accum = cap->GetAccumData();
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
	cap->Normalize();
	cap->IntegrateUIs(1);	//have to put something here, but we don't have the true count value

	//Check against the eye pattern
	auto rate = chan->GetMask().CalculateHitRate(
		cap,
		256,
		256,
		vrange,
		256.0 / (2*cap->m_uiWidth),
		-cap->m_uiWidth);
	GetChannel(i)->SetScalarValue(BERTInputChannel::STREAM_MASKHITRATE, rate);
	cap->SetMaskHitRate(rate);

	cap->MarkModifiedFromCpu();
	*/
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
