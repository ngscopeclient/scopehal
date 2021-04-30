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
	, m_depth(100e3)
	, m_rate(100e9)
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
	SetChannelDisplayName(0, "waveform");

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

Oscilloscope::TriggerMode SignalGeneratorOscilloscope::PollTrigger()
{
	if(m_triggerArmed)
		return TRIGGER_MODE_TRIGGERED;
	else
		return TRIGGER_MODE_STOP;
}

bool SignalGeneratorOscilloscope::AcquireData()
{
	//cap waveform rate at 25 wfm/s to avoid saturating cpu etc with channel emulation
	std::this_thread::sleep_for(std::chrono::microseconds(40 * 1000));

	auto waveform = m_bufmodel->SimulatePRBS(
		rand(),
		CORNER_TYP,
		FS_PER_SECOND / m_rate,
		m_depth,
		80			//1.25 Gbps
		);

	SequenceSet s;
	s[m_channels[0]] = waveform;

	m_pendingWaveformsMutex.lock();
	m_pendingWaveforms.push_back(s);
	m_pendingWaveformsMutex.unlock();

	//Update channel voltage ranges
	float lo = Filter::GetMinVoltage(waveform);
	float hi = Filter::GetMaxVoltage(waveform);
	float delta = hi - lo;

	m_channelVoltageRange[0] = delta * 1.2;
	m_channelOffset[0] = -(lo + delta/2);

	if(m_triggerOneShot)
		m_triggerArmed = false;

	return true;
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

vector<OscilloscopeChannel::CouplingType> SignalGeneratorOscilloscope::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	ret.push_back(OscilloscopeChannel::COUPLE_DC_50);
	return ret;
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
	uint64_t k = 1000;
	uint64_t m = k * k;
	uint64_t g = k * m;

	vector<uint64_t> ret;
	ret.push_back(1 * g);
	ret.push_back(5 * g);
	ret.push_back(10 * g);
	ret.push_back(25 * g);
	ret.push_back(50 * g);
	ret.push_back(100 * g);
	return ret;
}

vector<uint64_t> SignalGeneratorOscilloscope::GetSampleRatesInterleaved()
{
	return GetSampleRatesNonInterleaved();
}

set<Oscilloscope::InterleaveConflict> SignalGeneratorOscilloscope::GetInterleaveConflicts()
{
	//no-op
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> SignalGeneratorOscilloscope::GetSampleDepthsNonInterleaved()
{
	uint64_t k = 1000;
	uint64_t m = k * k;

	vector<uint64_t> ret;
	ret.push_back(10 * k);
	ret.push_back(100 * k);
	ret.push_back(1 * m);
	ret.push_back(10 * m);
	return ret;
}

vector<uint64_t> SignalGeneratorOscilloscope::GetSampleDepthsInterleaved()
{
	return GetSampleDepthsNonInterleaved();
}

uint64_t SignalGeneratorOscilloscope::GetSampleRate()
{
	return m_rate;
}

uint64_t SignalGeneratorOscilloscope::GetSampleDepth()
{
	return m_depth;
}

void SignalGeneratorOscilloscope::SetSampleDepth(uint64_t depth)
{
	m_depth = depth;
}

void SignalGeneratorOscilloscope::SetSampleRate(uint64_t rate)
{
	m_rate = rate;
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

bool SignalGeneratorOscilloscope::IsInterleaving()
{
	return false;
}

bool SignalGeneratorOscilloscope::SetInterleaving(bool /*combine*/)
{
	return false;
}

void SignalGeneratorOscilloscope::PushTrigger()
{
	//no-op
}

void SignalGeneratorOscilloscope::PullTrigger()
{
	//no-op
}
