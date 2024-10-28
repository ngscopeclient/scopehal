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
	@author Andrew D. Zonenberg
	@brief Implementation of UHDBridgeSDR
	@ingroup sdrdrivers
 */

#ifdef _WIN32
#include <chrono>
#include <thread>
#endif

#include "scopehal.h"
#include "ComplexChannel.h"
#include "UHDBridgeSDR.h"
#include "EdgeTrigger.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Construction / destruction

/**
	@brief Constructs a new driver object

	@param transport	SCPITransport connected to a scopehal-uhd-bridge server
 */
UHDBridgeSDR::UHDBridgeSDR(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, RemoteBridgeOscilloscope(transport)
{
	//Set up initial cache configuration as "not valid" and let it populate as we go

	IdentifyHardware();

	//For now add a single channel called "RX"
	//TODO: properly enumerate channels
	auto chan = new ComplexChannel(
		this,
		"RX",
		GetChannelColor(0),
		Unit(Unit::UNIT_FS),
		Unit(Unit::UNIT_VOLTS),
		m_channels.size());
	m_channels.push_back(chan);
	chan->SetDefaultDisplayName();

	//For now, hard code refclk until we implement a UI for that
	m_transport->SendCommandQueued("REFCLK internal");

	//Default to full scale range
	SetChannelOffset(0, 0, 0);
	SetChannelOffset(0, 1, 0);
	SetChannelVoltageRange(0, 0, 2);
	SetChannelVoltageRange(0, 1, 2);

	//Set initial config to 100K points (should be supported by everything??)
	//and fastest rate supported
	auto rates = GetSampleRatesNonInterleaved();
	SetSampleRate(rates[0]);
	SetSampleDepth(100000);

	//Set initial config
	SetCenterFrequency(0, 1000 * 1000 * 1000);
	SetSpan(10 * 1000 * 1000);

	//For now, hard code gain until we implement a UI for that
	m_transport->SendCommandQueued("RXGAIN 35");
}

/**
	@brief Color the channels arbitrarily (yellow-cyan-magenta-green)

	@param i	Channel number
 */
string UHDBridgeSDR::GetChannelColor(size_t i)
{
	switch(i % 4)
	{
		case 0:
			return "#ffd700";

		case 1:
			return "#00bfff";

		case 2:
			return "#ff00ff";

		case 3:
		default:
			return "#00ff00";
	}
}

/**
	@brief Query the hardware to determine capabilities of the instrument
 */
void UHDBridgeSDR::IdentifyHardware()
{
	//TODO: figure out what we are
}

UHDBridgeSDR::~UHDBridgeSDR()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Accessors

unsigned int UHDBridgeSDR::GetInstrumentTypes() const
{
	return Instrument::INST_OSCILLOSCOPE;
}

uint32_t UHDBridgeSDR::GetInstrumentTypesForChannel(size_t /*i*/) const
{
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Device interface functions

bool UHDBridgeSDR::HasTimebaseControls()
{
	return true;
}

bool UHDBridgeSDR::HasFrequencyControls()
{
	return true;
}

///@brief Return the constant driver name string "uhdbridge"
string UHDBridgeSDR::GetDriverNameInternal()
{
	return "uhdbridge";
}

void UHDBridgeSDR::FlushConfigCache()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
}

OscilloscopeChannel* UHDBridgeSDR::GetExternalTrigger()
{
	return nullptr;
}

bool UHDBridgeSDR::IsChannelEnabled(size_t /*i*/)
{
	return true;
}

void UHDBridgeSDR::EnableChannel(size_t /*i*/)
{
	//no-op until we support >1 channel
}

void UHDBridgeSDR::DisableChannel(size_t /*i*/)
{
	//no-op until we support >1 channel
}

unsigned int UHDBridgeSDR::GetChannelBandwidthLimit(size_t /*i*/)
{
	return 0;
}

void UHDBridgeSDR::SetChannelBandwidthLimit(size_t /*i*/, unsigned int /*limit_mhz*/)
{
	//no-op
}

float UHDBridgeSDR::GetChannelVoltageRange(size_t i, size_t stream)
{
	return RemoteBridgeOscilloscope::GetChannelVoltageRange(i, stream);
}

void UHDBridgeSDR::SetChannelVoltageRange(size_t i, size_t stream, float range)
{
	RemoteBridgeOscilloscope::SetChannelVoltageRange(i, stream, range);
}

float UHDBridgeSDR::GetChannelOffset(size_t i, size_t stream)
{
	return RemoteBridgeOscilloscope::GetChannelOffset(i, stream);
}

void UHDBridgeSDR::SetChannelOffset(size_t i, size_t stream, float offset)
{
	RemoteBridgeOscilloscope::SetChannelOffset(i, stream, offset);
}

OscilloscopeChannel::CouplingType UHDBridgeSDR::GetChannelCoupling(size_t /*i*/)
{
	return OscilloscopeChannel::COUPLE_AC_50;
}

void UHDBridgeSDR::SetChannelCoupling(size_t /*i*/, OscilloscopeChannel::CouplingType /*type*/)
{
	//no-op, coupling cannot be changed
}

vector<OscilloscopeChannel::CouplingType> UHDBridgeSDR::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	ret.push_back(OscilloscopeChannel::COUPLE_AC_50);
	return ret;
}

double UHDBridgeSDR::GetChannelAttenuation(size_t /*i*/)
{
	//TODO: frontend gain control via this API??
	return 1;
}

void UHDBridgeSDR::SetChannelAttenuation(size_t /*i*/, double /*atten*/)
{
	//no-op
}

bool UHDBridgeSDR::HasResolutionBandwidth()
{
	return false;
}

bool UHDBridgeSDR::CanInterleave()
{
	return false;
}

void UHDBridgeSDR::SetSpan(int64_t span)
{
	m_span = span;
	m_transport->SendCommandQueued(string("RXBW ") + to_string(span));
}

int64_t UHDBridgeSDR::GetSpan()
{
	return m_span;
}

void UHDBridgeSDR::SetCenterFrequency(size_t /*channel*/, int64_t freq)
{
	m_centerFreq = freq;
	m_transport->SendCommandQueued(string("RXFREQ ") + to_string(freq));
}

int64_t UHDBridgeSDR::GetCenterFrequency(size_t /*channel*/)
{
	return m_centerFreq;
}

uint64_t UHDBridgeSDR::GetSampleRate()
{
	return RemoteBridgeOscilloscope::GetSampleRate();
}

vector<uint64_t> UHDBridgeSDR::GetSampleRatesNonInterleaved()
{
	vector<uint64_t> ret;

	string rates = m_transport->SendCommandQueuedWithReply("RATES?");

	size_t i=0;
	while(true)
	{
		size_t istart = i;
		i = rates.find(',', i+1);
		if(i == string::npos)
			break;

		auto block = rates.substr(istart, i-istart);
		auto fs = stoll(block);
		auto hz = FS_PER_SECOND / fs;
		ret.push_back(hz);

		//skip the comma
		i++;
	}

	return ret;
}

vector<uint64_t> UHDBridgeSDR::GetSampleDepthsNonInterleaved()
{
	vector<uint64_t> ret;

	string depths = m_transport->SendCommandQueuedWithReply("DEPTHS?");

	size_t i=0;
	while(true)
	{
		size_t istart = i;
		i = depths.find(',', i+1);
		if(i == string::npos)
			break;

		uint64_t sampleDepth = stoull(depths.substr(istart, i-istart));
		ret.push_back(sampleDepth);

		//skip the comma
		i++;
	}

	return ret;
}

void UHDBridgeSDR::SetTriggerOffset(int64_t /*offset*/)
{
	//no-op
}

int64_t UHDBridgeSDR::GetTriggerOffset()
{
	return 0;
}

void UHDBridgeSDR::SetSampleRate(uint64_t rate)
{
	RemoteBridgeOscilloscope::SetSampleRate(rate);
}

Oscilloscope::TriggerMode UHDBridgeSDR::PollTrigger()
{
	//Always report "triggered" so we can block on AcquireData() in ScopeThread
	//TODO: peek function of some sort?
	return TRIGGER_MODE_TRIGGERED;
}

bool UHDBridgeSDR::AcquireData()
{
	SequenceSet s;
	double now = GetTime();

	//For now hard code single channel until we support more
	size_t numChannels = 1;

	if(numChannels == 0)
		return false;

	//Acquire data for each channel
	for(size_t i=0; i<numChannels; i++)
	{
		//Read the number of samples in the buffer (may be different from current depth if we just changed it)
		uint64_t depth;
		if(!m_transport->ReadRawData(sizeof(depth), (uint8_t*)&depth))
			return false;

		//Get the sample rate
		int64_t sample_hz;
		if(!m_transport->ReadRawData(sizeof(sample_hz), (uint8_t*)&sample_hz))
			return false;
		int64_t fs_per_sample = FS_PER_SECOND / sample_hz;

		//Allocate the samples
		float* buf = new float[depth*2];

		//TODO: stream timestamp from the server

		if(!m_transport->ReadRawData(depth * sizeof(float) * 2, (uint8_t*)buf))
			return false;

		//Create our waveforms
		auto icap = AllocateAnalogWaveform(m_nickname + "." + GetOscilloscopeChannel(i)->GetHwname() + ".i");
		icap->m_timescale = fs_per_sample;
		icap->m_triggerPhase = 0;
		icap->m_startTimestamp = floor(now);
		icap->m_startFemtoseconds = (now - floor(now)) * FS_PER_SECOND;
		icap->Resize(depth);

		auto qcap = AllocateAnalogWaveform(m_nickname + "." + GetOscilloscopeChannel(i)->GetHwname() + ".q");
		qcap->m_timescale = fs_per_sample;
		qcap->m_triggerPhase = 0;
		qcap->m_startTimestamp = floor(now);
		qcap->m_startFemtoseconds = (now - floor(now)) * FS_PER_SECOND;
		qcap->Resize(depth);

		//De-interleave the I and Q samples
		//TODO: do this in a shader
		icap->PrepareForCpuAccess();
		qcap->PrepareForCpuAccess();
		for(size_t j=0; j<depth; j++)
		{
			icap->m_samples[j] = buf[j*2];
			qcap->m_samples[j] = buf[j*2 + 1];
		}
		icap->MarkSamplesModifiedFromCpu();
		qcap->MarkSamplesModifiedFromCpu();

		s[StreamDescriptor(GetChannel(i), 0)] = icap;
		s[StreamDescriptor(GetChannel(i), 1)] = qcap;

		//Clean up
		delete[] buf;
	}

	//Save the waveforms to our queue
	m_pendingWaveformsMutex.lock();
	m_pendingWaveforms.push_back(s);
	m_pendingWaveformsMutex.unlock();

	//If this was a one-shot trigger we're no longer armed
	if(m_triggerOneShot)
		m_triggerArmed = false;

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Logic analyzer configuration
