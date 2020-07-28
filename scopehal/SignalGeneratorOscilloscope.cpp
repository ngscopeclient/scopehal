/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2020 Andrew D. Zonenberg                                                                          *
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
	@author Andrew D. Zonenberg
	@brief Implementation of SignalGeneratorOscilloscope
 */

#include "scopehal.h"
#include "OscilloscopeChannel.h"
#include "SignalGeneratorOscilloscope.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SignalGeneratorOscilloscope::SignalGeneratorOscilloscope(SCPITransport* transport)
	: SCPIOscilloscope(transport, false)
	, m_extTrigger(NULL)
	, m_triggerArmed(false)
	, m_triggerOneShot(false)
{
	//Create a single channel named "waveform"
	//TODO: more flexible config
	m_channels.push_back(
		new OscilloscopeChannel(
		this,
		"waveform",
		OscilloscopeChannel::CHANNEL_TYPE_ANALOG,
		"#ffff80",
		1,
		0,
		true));

	m_model = "IBIS Signal Generator";
	m_vendor = "Antikernel Labs";
	m_serial = "N/A";

	//TODO: have arguments for this
	m_parser.Load("/nfs4/share/datasheets/Xilinx/7_series/kintex-7/kintex7.ibs");
	m_bufmodel = m_parser.m_models["LVDS_HP_O"];

	//Configure channel
	m_channelsEnabled[0] = true;
	m_channelCoupling[0] = OscilloscopeChannel::COUPLE_SYNTHETIC;
	m_channelAttenuation[0] = 1;
	m_channelBandwidth[0] = 0;

	//Voltage range (allow 10% extra for overshoot)
	float vcc = m_bufmodel->m_voltages[CORNER_TYP];
	m_channelVoltageRange[0] = vcc * 1.1;
	m_channelOffset[0] = -vcc/2;
}

SignalGeneratorOscilloscope::~SignalGeneratorOscilloscope()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Information queries

string SignalGeneratorOscilloscope::IDPing()
{
	return "";
}

string SignalGeneratorOscilloscope::GetDriverNameInternal()
{
	return "siggen";
}

unsigned int SignalGeneratorOscilloscope::GetInstrumentTypes()
{
	return INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering

Oscilloscope::TriggerType SignalGeneratorOscilloscope::GetTriggerType()
{
	return TRIGGER_TYPE_RISING;
}

void SignalGeneratorOscilloscope::SetTriggerType(Oscilloscope::TriggerType /*type*/)
{
	//no-op, we never trigger
}

Oscilloscope::TriggerMode SignalGeneratorOscilloscope::PollTrigger()
{
	if(m_triggerArmed)
		return TRIGGER_MODE_TRIGGERED;
	else
		return TRIGGER_MODE_STOP;
}

bool SignalGeneratorOscilloscope::AcquireData(bool toQueue)
{
	//cap waveform rate at 50 wfm/s to avoid saturating cpu etc
	usleep(20 * 1000);

	auto waveform = m_bufmodel->SimulatePRBS(
		rand(),
		CORNER_TYP,
		10,			//100 Gsps
		200000,
		80			//1.25 Gbps
		);

	if(toQueue)
	{
		m_pendingWaveformsMutex.lock();
		SequenceSet s;
		s[m_channels[0]] = waveform;
		m_pendingWaveforms.push_back(s);
		m_pendingWaveformsMutex.unlock();
	}
	else
		m_channels[0]->SetData(waveform);

	//Update channel voltage ranges
	float lo = ProtocolDecoder::GetMinVoltage(waveform);
	float hi = ProtocolDecoder::GetMaxVoltage(waveform);
	float delta = hi - lo;

	m_channelVoltageRange[0] = delta * 1.2;
	m_channelOffset[0] = -(lo + delta/2);

	if(m_triggerOneShot)
		m_triggerArmed = false;

	return true;
}

void SignalGeneratorOscilloscope::ArmTrigger()
{
}

void SignalGeneratorOscilloscope::StartSingleTrigger()
{
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void SignalGeneratorOscilloscope::Start()
{
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void SignalGeneratorOscilloscope::Stop()
{
	m_triggerArmed = false;
	m_triggerOneShot = false;
}

bool SignalGeneratorOscilloscope::IsTriggerArmed()
{
	return m_triggerArmed;
}

void SignalGeneratorOscilloscope::ResetTriggerConditions()
{
	//no-op, we never trigger
}

void SignalGeneratorOscilloscope::SetTriggerForChannel(OscilloscopeChannel* /*channel*/, vector<TriggerType> /*triggerbits*/)
{
	//no-op, we never trigger
}

size_t SignalGeneratorOscilloscope::GetTriggerChannelIndex()
{
	return 0;
}

void SignalGeneratorOscilloscope::SetTriggerChannelIndex(size_t /*i*/)
{
	//no-op, we never trigger
}

float SignalGeneratorOscilloscope::GetTriggerVoltage()
{
	return 0;
}

void SignalGeneratorOscilloscope::SetTriggerVoltage(float /*v*/)
{
	//no-op, we never trigger
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Channel configuration. Mostly trivial stubs.

bool SignalGeneratorOscilloscope::IsChannelEnabled(size_t i)
{
	return m_channelsEnabled[i];
}

void SignalGeneratorOscilloscope::EnableChannel(size_t i)
{
	m_channelsEnabled[i] = true;
}

void SignalGeneratorOscilloscope::DisableChannel(size_t i)
{
	m_channelsEnabled[i] = false;
}

OscilloscopeChannel::CouplingType SignalGeneratorOscilloscope::GetChannelCoupling(size_t i)
{
	return m_channelCoupling[i];
}

void SignalGeneratorOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	m_channelCoupling[i] = type;
}

double SignalGeneratorOscilloscope::GetChannelAttenuation(size_t i)
{
	return m_channelAttenuation[i];
}

void SignalGeneratorOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	m_channelAttenuation[i] = atten;
}

int SignalGeneratorOscilloscope::GetChannelBandwidthLimit(size_t i)
{
	return m_channelBandwidth[i];
}

void SignalGeneratorOscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
{
	m_channelBandwidth[i] = limit_mhz;
}

double SignalGeneratorOscilloscope::GetChannelVoltageRange(size_t i)
{
	return m_channelVoltageRange[i];
}

void SignalGeneratorOscilloscope::SetChannelVoltageRange(size_t i, double range)
{
	m_channelVoltageRange[i] = range;
}

OscilloscopeChannel* SignalGeneratorOscilloscope::GetExternalTrigger()
{
	return m_extTrigger;
}

double SignalGeneratorOscilloscope::GetChannelOffset(size_t i)
{
	return m_channelOffset[i];
}

void SignalGeneratorOscilloscope::SetChannelOffset(size_t i, double offset)
{
	m_channelOffset[i] = offset;
}

vector<uint64_t> SignalGeneratorOscilloscope::GetSampleRatesNonInterleaved()
{
	//no-op
	vector<uint64_t> ret;
	return ret;
}

vector<uint64_t> SignalGeneratorOscilloscope::GetSampleRatesInterleaved()
{
	//no-op
	vector<uint64_t> ret;
	return ret;
}

set<Oscilloscope::InterleaveConflict> SignalGeneratorOscilloscope::GetInterleaveConflicts()
{
	//no-op
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> SignalGeneratorOscilloscope::GetSampleDepthsNonInterleaved()
{
	//no-op
	vector<uint64_t> ret;
	return ret;
}

vector<uint64_t> SignalGeneratorOscilloscope::GetSampleDepthsInterleaved()
{
	//no-op
	vector<uint64_t> ret;
	return ret;
}

uint64_t SignalGeneratorOscilloscope::GetSampleRate()
{
	return 1;
}

uint64_t SignalGeneratorOscilloscope::GetSampleDepth()
{
	//FIXME
	return 1;
}

void SignalGeneratorOscilloscope::SetSampleDepth(uint64_t /*depth*/)
{
	//no-op
}

void SignalGeneratorOscilloscope::SetSampleRate(uint64_t /*rate*/)
{
	//no-op
}

void SignalGeneratorOscilloscope::SetTriggerOffset(int64_t /*offset*/)
{
	//FIXME
}

int64_t SignalGeneratorOscilloscope::GetTriggerOffset()
{
	//FIXME
	return 0;
}
