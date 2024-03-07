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
#include "SocketCANAnalyzer.h"
#include "EdgeTrigger.h"

#ifdef __linux

#include <linux/can.h>
#include <linux/can/raw.h>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SocketCANAnalyzer::SocketCANAnalyzer(SCPITransport* transport)
	: SCPIDevice(transport, false)
	, SCPIInstrument(transport, false)
	, m_triggerArmed(false)
	, m_triggerOneShot(false)
	, m_appendingNext(false)
	, m_startSec(0)
	, m_startNsec(0)
{
	auto chan = new CANChannel(this, "CAN", "#808080", 0);
	m_channels.push_back(chan);
}

SocketCANAnalyzer::~SocketCANAnalyzer()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string SocketCANAnalyzer::GetDriverNameInternal()
{
	return "socketcan";
}

unsigned int SocketCANAnalyzer::GetInstrumentTypes() const
{
	return Instrument::INST_OSCILLOSCOPE;
}

uint32_t SocketCANAnalyzer::GetInstrumentTypesForChannel(size_t /*i*/) const
{
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device interface functions

void SocketCANAnalyzer::FlushConfigCache()
{
}

OscilloscopeChannel* SocketCANAnalyzer::GetExternalTrigger()
{
	return nullptr;
}

bool SocketCANAnalyzer::IsChannelEnabled(size_t /*i*/)
{
	return true;
}

void SocketCANAnalyzer::EnableChannel(size_t /*i*/)
{
}

void SocketCANAnalyzer::DisableChannel(size_t /*i*/)
{
}

vector<OscilloscopeChannel::CouplingType> SocketCANAnalyzer::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	return ret;
}

OscilloscopeChannel::CouplingType SocketCANAnalyzer::GetChannelCoupling(size_t /*i*/)
{
	return OscilloscopeChannel::COUPLE_DC_50;
}

void SocketCANAnalyzer::SetChannelCoupling(size_t /*i*/, OscilloscopeChannel::CouplingType /*type*/)
{
}

double SocketCANAnalyzer::GetChannelAttenuation(size_t /*i*/)
{
	return 0;
}

void SocketCANAnalyzer::SetChannelAttenuation(size_t /*i*/, double /*atten*/)
{
}

string SocketCANAnalyzer::GetProbeName(size_t /*i*/)
{
	return "";
}

unsigned int SocketCANAnalyzer::GetChannelBandwidthLimit(size_t /*i*/)
{
	return 0;
}

void SocketCANAnalyzer::SetChannelBandwidthLimit(size_t /*i*/, unsigned int /*limit_mhz*/)
{
}

vector<unsigned int> SocketCANAnalyzer::GetChannelBandwidthLimiters(size_t /*i*/)
{
	vector<unsigned int> ret;
	return ret;
}

float SocketCANAnalyzer::GetChannelVoltageRange(size_t /*i*/, size_t /*stream*/)
{
	return 0;
}

void SocketCANAnalyzer::SetChannelVoltageRange(size_t /*i*/, size_t /*stream*/, float /*range*/)
{
}

float SocketCANAnalyzer::GetChannelOffset(size_t /*i*/, size_t /*stream*/)
{
	return 0;
}

void SocketCANAnalyzer::SetChannelOffset(size_t /*i*/, size_t /*stream*/, float /*offset*/)
{
}

//////////////////////////////////////////////////////////////////////////////// </Digital>

Oscilloscope::TriggerMode SocketCANAnalyzer::PollTrigger()
{
	if (!m_triggerArmed)
		return TRIGGER_MODE_STOP;
	return TRIGGER_MODE_TRIGGERED;
}

bool SocketCANAnalyzer::IsAppendingToWaveform()
{
	return m_appendingNext;
}

bool SocketCANAnalyzer::PopPendingWaveform()
{
	lock_guard<mutex> lock(m_pendingWaveformsMutex);
	if(m_pendingWaveforms.size())
	{
		SequenceSet set = *m_pendingWaveforms.begin();
		for(auto it : set)
		{
			auto chan = it.first.m_channel;
			auto data = dynamic_cast<CANWaveform*>(it.second);
			auto nstream = it.first.m_stream;

			//If there is an existing waveform, append to it
			//TODO: make this more efficient
			auto oldWaveform = dynamic_cast<CANWaveform*>(chan->GetData(nstream));
			if(oldWaveform && data && m_appendingNext)
			{
				size_t len = data->size();
				oldWaveform->PrepareForCpuAccess();
				data->PrepareForCpuAccess();
				for(size_t i=0; i<len; i++)
				{
					oldWaveform->m_samples.push_back(data->m_samples[i]);
					oldWaveform->m_offsets.push_back(data->m_offsets[i]);
					oldWaveform->m_durations.push_back(data->m_durations[i]);
				}
				oldWaveform->m_revision ++;
				oldWaveform->MarkModifiedFromCpu();
			}
			else
				chan->SetData(data, nstream);
		}
		m_pendingWaveforms.pop_front();

		m_appendingNext = true;
		return true;
	}
	return false;
}

bool SocketCANAnalyzer::AcquireData()
{
	auto transport = dynamic_cast<SCPISocketCANTransport*>(m_transport);
	if(!transport)
		return false;

	//Get the existing waveform if we have one
	//TODO: Start a new waveform only if a new trigger cycle

	auto cap = new CANWaveform;
	cap->m_timescale = 1;
	cap->m_startTimestamp = m_startSec;
	cap->m_startFemtoseconds = m_startNsec * 1e6;
	cap->m_triggerPhase = 0;
	cap->PrepareForCpuAccess();

	//Add timeline samples (fake durations assuming 250 Kbps for now)
	//TODO make this configurable
	int64_t ui = 4 * 1000LL * 1000LL * 1000LL;

	//Read frames until we run out or a timeout elapses
	size_t npackets = 0;
	int64_t tLastEnd = 0;
	while(true)
	{
		//Grab a frame and stop capturing if nothing shows up within the timeout window
		can_frame frame;
		int64_t sec;
		int64_t ns;
		int nbytes = transport->ReadPacket(&frame, sec, ns);
		if(nbytes < 0)
			break;

		//Calculate delay since start of capture, wrapping properly around second boundaries
		int64_t dsec = sec - m_startSec;
		int64_t dnsec = ns - m_startNsec;
		if(dnsec < 0)
		{
			dsec --;
			dnsec += 1e9;
		}

		int64_t trel = dsec * FS_PER_SECOND + dnsec*1e6;

		//if last packet hasnt ended, there was a timestamping roundoff
		//bump our start to match
		if(trel <= tLastEnd)
			trel = tLastEnd + ui;

		//bool ext = (frame.can_id & CAN_EFF_MASK) > 2047;
		bool rtr = (frame.can_id & CAN_RTR_FLAG) == CAN_RTR_FLAG;

		cap->m_offsets.push_back(trel);
		cap->m_durations.push_back(ui);
		cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_SOF, 0));

		cap->m_offsets.push_back(trel + ui);
		cap->m_durations.push_back(31 * ui);
		cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_ID, frame.can_id));

		cap->m_offsets.push_back(trel + 32*ui);
		cap->m_durations.push_back(ui);
		cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_RTR, rtr));

		cap->m_offsets.push_back(trel + 33*ui);
		cap->m_durations.push_back(ui);
		cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_FD, 0));

		cap->m_offsets.push_back(trel + 34*ui);
		cap->m_durations.push_back(ui);
		cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_R0, 0));

		cap->m_offsets.push_back(trel + 35*ui);
		cap->m_durations.push_back(ui*4);
		cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_DLC, frame.can_dlc));

		//Data
		for(int i=0; i<frame.can_dlc; i++)
		{
			cap->m_offsets.push_back(trel + 39*ui + i*8*ui);
			cap->m_durations.push_back(ui*8);
			cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_DATA, frame.data[i]));
		}

		//Find end of the packet
		tLastEnd = trel + (39 + (frame.can_dlc*8)) * ui;

		//Every 100 packets check timeout, after 50ms of acquisition stop
		npackets ++;
		if( (npackets % 100) == 0)
		{
			//get elapsed time
			timespec t;
			clock_gettime(CLOCK_REALTIME,&t);
			dsec = t.tv_sec - m_startSec;
			dnsec = t.tv_nsec - m_startNsec;
			if(dnsec < 0)
			{
				dsec --;
				dnsec += 1e9;
			}

			if( (dsec > 1) || (dnsec > 5e7) )
				break;
		}
	}

	cap->MarkModifiedFromCpu();

	//Save newly created waveform
	m_pendingWaveformsMutex.lock();
		SequenceSet s;
		s[m_channels[0]] = cap;
		m_pendingWaveforms.push_back(s);
	m_pendingWaveformsMutex.unlock();

	if(m_triggerOneShot)
		m_triggerArmed = false;

	return true;
}

void SocketCANAnalyzer::Start()
{
	m_triggerArmed = true;
	m_triggerOneShot = false;

	m_appendingNext = false;

	timespec t;
	clock_gettime(CLOCK_REALTIME,&t);
	m_startSec = t.tv_sec;
	m_startNsec = t.tv_nsec;
}

void SocketCANAnalyzer::StartSingleTrigger()
{
	m_triggerArmed = true;
	m_triggerOneShot = true;

	m_appendingNext = false;

	timespec t;
	clock_gettime(CLOCK_REALTIME,&t);
	m_startSec = t.tv_sec;
	m_startNsec = t.tv_nsec;
}

void SocketCANAnalyzer::Stop()
{
	m_triggerArmed = false;
	m_triggerOneShot = true;
}

void SocketCANAnalyzer::ForceTrigger()
{
	StartSingleTrigger();
}

bool SocketCANAnalyzer::IsTriggerArmed()
{
	return m_triggerArmed;
}

vector<uint64_t> SocketCANAnalyzer::GetSampleRatesNonInterleaved()
{
	vector<uint64_t> ret;
	return ret;
}

vector<uint64_t> SocketCANAnalyzer::GetSampleRatesInterleaved()
{
	return GetSampleRatesNonInterleaved();
}

set<Oscilloscope::InterleaveConflict> SocketCANAnalyzer::GetInterleaveConflicts()
{
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> SocketCANAnalyzer::GetSampleDepthsNonInterleaved()
{
	vector<uint64_t> ret;
	return ret;
}

vector<uint64_t> SocketCANAnalyzer::GetSampleDepthsInterleaved()
{
	return GetSampleRatesNonInterleaved();
}

uint64_t SocketCANAnalyzer::GetSampleRate()
{
	return 1;
}

uint64_t SocketCANAnalyzer::GetSampleDepth()
{
	return 1;
}

void SocketCANAnalyzer::SetSampleDepth(uint64_t /*depth*/)
{
}

void SocketCANAnalyzer::SetSampleRate(uint64_t /*rate*/)
{

}

void SocketCANAnalyzer::SetTriggerOffset(int64_t /*offset*/)
{
}

int64_t SocketCANAnalyzer::GetTriggerOffset()
{
	return 0;
}

bool SocketCANAnalyzer::IsInterleaving()
{
	return false;
}

bool SocketCANAnalyzer::SetInterleaving(bool /*combine*/)
{
	return false;
}

void SocketCANAnalyzer::PullTrigger()
{
}

void SocketCANAnalyzer::PushTrigger()
{
}
#endif
