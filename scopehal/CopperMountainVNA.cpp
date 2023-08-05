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
#include "CopperMountainVNA.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

CopperMountainVNA::CopperMountainVNA(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, m_rbw(1)
{
	//For now, assume we're a 2-port VNA only

	//Add analog channel objects
	for(size_t dest = 0; dest<2; dest ++)
	{
		for(size_t src=0; src<2; src++)
		{
			//Hardware name of the channel
			string chname = "S" + to_string(dest+1) + to_string(src+1);

			//Create the channel
			auto ichan = m_channels.size();
			auto chan = new SParameterChannel(
				this,
				chname,
				GetChannelColor(ichan),
				ichan);
			m_channels.push_back(chan);
			chan->SetDefaultDisplayName();
			chan->SetXAxisUnits(Unit::UNIT_HZ);

			//Set initial configuration so we have a well-defined instrument state
			SetChannelVoltageRange(ichan, 0, 80);
			SetChannelOffset(ichan, 0, 40);
			SetChannelVoltageRange(ichan, 1, 360);
			SetChannelOffset(ichan, 1, 0);
		}
	}

	//Apparently binary data transfer is not supported over TCP sockets since they ONLY use newline as end of message
	//while HiSLIP does not support pipelining of commands? That's derpy...

	//Set trigger source to internal
	m_transport->SendCommandQueued("TRIG:SOUR INT");

	//Turn off continuous trigger sweep
	m_transport->SendCommandQueued("INIT:CONT OFF");

	//Turn on RF power
	m_transport->SendCommandQueued("OUTP ON");

	//Set the channels we want to look at
	m_transport->SendCommandQueued("CALC:PAR1:DEF S11");
	m_transport->SendCommandQueued("CALC:PAR2:DEF S21");
	m_transport->SendCommandQueued("CALC:PAR3:DEF S12");
	m_transport->SendCommandQueued("CALC:PAR4:DEF S22");

	//Format polar (real + imag)
	m_transport->SendCommandQueued("CALC:TRAC1:FORM POL");
	m_transport->SendCommandQueued("CALC:TRAC2:FORM POL");
	m_transport->SendCommandQueued("CALC:TRAC3:FORM POL");
	m_transport->SendCommandQueued("CALC:TRAC4:FORM POL");

	//Get and cache resolution bandwidth
	auto srbw = m_transport->SendCommandQueuedWithReply("SENS:BWID?");
	Unit hz(Unit::UNIT_HZ);
	m_rbw = hz.ParseString(srbw);

	//Get and cache memory depth
	auto sdepth = m_transport->SendCommandQueuedWithReply("SENS:SWE:POIN?");
	m_memoryDepth = stoi(srbw);

	//Get and cache start frequency
	auto sfreq = m_transport->SendCommandQueuedWithReply("SENS:FREQ:STAR?");
	m_sweepStart = hz.ParseString(sfreq);
	sfreq = m_transport->SendCommandQueuedWithReply("SENS:FREQ:STOP?");
	m_sweepStop = hz.ParseString(sfreq);
}

CopperMountainVNA::~CopperMountainVNA()
{
	m_transport->SendCommandQueued("OUTP OFF");
}

/**
	@brief Color the channels (blue-red-green-yellow-purple-gray-cyan-magenta)
 */
string CopperMountainVNA::GetChannelColor(size_t i)
{
	switch(i % 8)
	{
		case 0:
			return "#4040ff";

		case 1:
			return "#ff4040";

		case 2:
			return "#208020";

		case 3:
			return "#ffff00";

		case 4:
			return "#600080";

		case 5:
			return "#808080";

		case 6:
			return "#40a0a0";

		case 7:
		default:
			return "#e040e0";
	}
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device enumeration

string CopperMountainVNA::GetDriverNameInternal()
{
	return "coppermt";
}

unsigned int CopperMountainVNA::GetInstrumentTypes()
{
	return Instrument::INST_OSCILLOSCOPE;
}

uint32_t CopperMountainVNA::GetInstrumentTypesForChannel(size_t /*i*/)
{
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Driver logic

bool CopperMountainVNA::IsChannelEnabled(size_t /*i*/)
{
	return true;
}

void CopperMountainVNA::EnableChannel(size_t /*i*/)
{
	//no-op
}

void CopperMountainVNA::DisableChannel(size_t /*i*/)
{
	//no-op
}

OscilloscopeChannel::CouplingType CopperMountainVNA::GetChannelCoupling(size_t /*i*/)
{
	//all inputs are ac coupled 50 ohm impedance
	return OscilloscopeChannel::COUPLE_AC_50;
}

void CopperMountainVNA::SetChannelCoupling(size_t /*i*/, OscilloscopeChannel::CouplingType /*type*/)
{
	//no-op, coupling cannot be changed
}

vector<OscilloscopeChannel::CouplingType> CopperMountainVNA::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	ret.push_back(OscilloscopeChannel::COUPLE_AC_50);
	return ret;
}

double CopperMountainVNA::GetChannelAttenuation(size_t /*i*/)
{
	return 1;
}

void CopperMountainVNA::SetChannelAttenuation(size_t /*i*/, double /*atten*/)
{
	//no-op
}

unsigned int CopperMountainVNA::GetChannelBandwidthLimit(size_t /*i*/)
{
	return 0;
}

void CopperMountainVNA::SetChannelBandwidthLimit(size_t /*i*/, unsigned int /*limit_mhz*/)
{
	//no-op
}

float CopperMountainVNA::GetChannelVoltageRange(size_t i, size_t stream)
{
	//range in cache is always valid
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelVoltageRange[pair<size_t, size_t>(i, stream)];
}

void CopperMountainVNA::SetChannelVoltageRange(size_t i, size_t stream, float range)
{
	//Range is entirely clientside, hardware is always full scale dynamic range
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelVoltageRange[pair<size_t, size_t>(i, stream)]= range;
}

float CopperMountainVNA::GetChannelOffset(size_t i, size_t stream)
{
	//offset in cache is always valid
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelOffset[pair<size_t, size_t>(i, stream)];
}

void CopperMountainVNA::SetChannelOffset(size_t i, size_t stream, float offset)
{
	//Offset is entirely clientside, hardware is always full scale dynamic range
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	m_channelOffset[pair<size_t, size_t>(i, stream)] = offset;
}

//TODO: support ext trig if any
OscilloscopeChannel* CopperMountainVNA::GetExternalTrigger()
{
	return nullptr;
}

Oscilloscope::TriggerMode CopperMountainVNA::PollTrigger()
{
	auto state = Trim(m_transport->SendCommandQueuedWithReply("TRIG:STAT?"));

	//Pending, but no data yet
	if( (state ==  "MEAS") || (state == "WTRG") )
		return TRIGGER_MODE_RUN;

	else //if(state == "HOLD")
	{
		if(m_triggerArmed)
			return TRIGGER_MODE_TRIGGERED;
		else
			return TRIGGER_MODE_STOP;
	}
}

void CopperMountainVNA::Start()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommandQueued("INIT:IMM");
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void CopperMountainVNA::StartSingleTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommandQueued("INIT:IMM");
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void CopperMountainVNA::Stop()
{
	m_transport->SendCommandQueued("ABOR");
	m_triggerArmed = false;
	m_triggerOneShot = false;
}

void CopperMountainVNA::ForceTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommandQueued("INIT:IMM");
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

bool CopperMountainVNA::IsTriggerArmed()
{
	//return m_triggerArmed;
	return true;
}

void CopperMountainVNA::PushTrigger()
{
}

void CopperMountainVNA::PullTrigger()
{
}

bool CopperMountainVNA::AcquireData()
{
	m_transport->SendCommandQueuedWithReply("*OPC?");

	SequenceSet s;
	double tstart = GetTime();
	int64_t fs = (tstart - floor(tstart)) * FS_PER_SECOND;

	for(size_t dest = 0; dest<2; dest ++)
	{
		for(size_t src=0; src<2; src++)
		{
			//Hardware name of the channel
			string chname = "S" + to_string(dest+1) + to_string(src+1);

			int nparam = dest*2 + src;
			auto sdata = m_transport->SendCommandQueuedWithReply(
				string("CALC:TRAC") + to_string(nparam+1) + ":DATA:FDAT?");

			auto values = explode(sdata, ',');
			size_t npoints = values.size() / 2;

			int64_t stepsize = (m_sweepStop - m_sweepStart) / npoints;

			//Create the waveforms
			auto mcap = new UniformAnalogWaveform;
			mcap->m_timescale = stepsize;
			mcap->m_triggerPhase = m_sweepStart;
			mcap->m_startTimestamp = floor(tstart);
			mcap->m_startFemtoseconds = fs;
			mcap->PrepareForCpuAccess();

			auto acap = new UniformAnalogWaveform;
			acap->m_timescale = stepsize;
			acap->m_triggerPhase = m_sweepStart;
			acap->m_startTimestamp = floor(tstart);
			acap->m_startFemtoseconds = fs;
			acap->PrepareForCpuAccess();

			//Make content for display (dB and degrees)
			mcap->Resize(npoints);
			acap->Resize(npoints);
			for(size_t i=0; i<npoints; i++)
			{
				float real = stof(values[i*2]);
				float imag = stof(values[i*2 + 1]);

				float mag = sqrt(real*real + imag*imag);
				float angle = atan2(imag, real);

				mcap->m_samples[i] = 20 * log10(mag);
				acap->m_samples[i] = angle * 180 / M_PI;
			}

			acap->MarkModifiedFromCpu();
			mcap->MarkModifiedFromCpu();

			auto chan = GetChannel(nparam);
			s[StreamDescriptor(chan, 0)] = mcap;
			s[StreamDescriptor(chan, 1)] = acap;
		}
	}

	//Save the waveforms to our queue
	m_pendingWaveformsMutex.lock();
	m_pendingWaveforms.push_back(s);
	m_pendingWaveformsMutex.unlock();

	//If this was a one-shot trigger we're no longer armed
	if(m_triggerOneShot)
		m_triggerArmed = false;

	//If continuous trigger, re-arm for another acquisition
	else if(m_triggerArmed)
	{
		//m_transport->SendCommand("*TRG");
		m_transport->SendCommandQueued("INIT:IMM");
	}

	return true;
}

vector<uint64_t> CopperMountainVNA::GetSampleRatesNonInterleaved()
{
	vector<uint64_t> ret;
	ret.push_back(1);
	return ret;
}

vector<uint64_t> CopperMountainVNA::GetSampleRatesInterleaved()
{
	//interleaving not supported
	vector<uint64_t> ret = {};
	return ret;
}

set<Oscilloscope::InterleaveConflict> CopperMountainVNA::GetInterleaveConflicts()
{
	//interleaving not supported
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> CopperMountainVNA::GetSampleDepthsNonInterleaved()
{
	vector<uint64_t> ret;
	ret.push_back(10000);
	return ret;
}

vector<uint64_t> CopperMountainVNA::GetSampleDepthsInterleaved()
{
	//interleaving not supported
	vector<uint64_t> ret;
	return ret;
}

uint64_t CopperMountainVNA::GetSampleRate()
{
	return 1;
}

uint64_t CopperMountainVNA::GetSampleDepth()
{
	return m_memoryDepth;
}

void CopperMountainVNA::SetSampleDepth(uint64_t /*depth*/)
{
}

void CopperMountainVNA::SetSampleRate(uint64_t /*rate*/)
{
}

void CopperMountainVNA::SetTriggerOffset(int64_t /*offset*/)
{
}

int64_t CopperMountainVNA::GetTriggerOffset()
{
	return 0;
}

bool CopperMountainVNA::IsInterleaving()
{
	return false;
}

bool CopperMountainVNA::SetInterleaving(bool /*combine*/)
{
	return false;
}

int64_t CopperMountainVNA::GetResolutionBandwidth()
{
	return m_rbw;
}

bool CopperMountainVNA::HasFrequencyControls()
{
	return true;
}

bool CopperMountainVNA::HasTimebaseControls()
{
	return false;
}
