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
	, m_tstart(0)
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
	//Get the existing waveform if we have one
	//TODO: Start a new waveform only if a new trigger cycle

	auto cap = new CANWaveform;
	cap->m_timescale = 1;
	cap->m_startTimestamp = floor(m_tstart);
	cap->m_startFemtoseconds = (m_tstart - cap->m_startTimestamp) * FS_PER_SECOND;
	cap->m_triggerPhase = 0;
	cap->PrepareForCpuAccess();

	//Read frames until we run out or a timeout elapses
	double tstart = GetTime();
	size_t npackets = 0;
	while(true)
	{
		//Grab a frame and stop capturing if nothing shows up within the timeout window
		can_frame frame;
		int nbytes = m_transport->ReadRawData(sizeof(frame), (uint8_t*)&frame);
		if(nbytes < 0)
			break;

		double delta = GetTime();
		delta -= m_tstart;
		int64_t trel = delta * FS_PER_SECOND;

		bool ext = (frame.can_id & CAN_EFF_MASK) > 2047;
		bool rtr = (frame.can_id & CAN_RTR_FLAG) == CAN_RTR_FLAG;

		//Add timeline samples (fake durations assuming 500 Kbps for now)
		//TODO make this configurable
		int64_t ui = 2 * 1000LL * 1000LL * 1000LL;
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

		//Every 100 packets check timeout, after 50ms of acquisition stop
		npackets ++;
		if( (npackets % 100) == 0)
		{
			double now = GetTime();
			if( (now - tstart) > 0.05)
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
	m_tstart = GetTime();
	m_appendingNext = false;
}

void SocketCANAnalyzer::StartSingleTrigger()
{
	m_triggerArmed = true;
	m_triggerOneShot = true;
	m_tstart = GetTime();
	m_appendingNext = false;
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
	/*
	lock_guard<recursive_mutex> lock(m_mutex);

	string resp = m_transport->SendCommandQueuedWithReply("TRIGGER1:TYPE?");

	if (resp == "EDGE")
		PullEdgeTrigger();
	else
	{
		LogWarning("Unknown Trigger Type. Forcing Edge.\n");

		delete m_trigger;

		m_trigger = new EdgeTrigger(this);
		EdgeTrigger* et = dynamic_cast<EdgeTrigger*>(m_trigger);

		et->SetType(EdgeTrigger::EDGE_RISING);
		et->SetInput(0, StreamDescriptor(GetChannelByHwName("CHAN1"), 0), true);
		et->SetLevel(1.0);
		PushTrigger();
		PullTrigger();
	}
	*/
}

/**
	@brief Reads settings for an edge trigger from the instrument
 *//*
void SocketCANAnalyzer::PullEdgeTrigger()
{
	if( (m_trigger != NULL) && (dynamic_cast<EdgeTrigger*>(m_trigger) != NULL) )
	{
		delete m_trigger;
		m_trigger = NULL;
	}

	//Create a new trigger if necessary
	if(m_trigger == NULL)
		m_trigger = new EdgeTrigger(this);
	EdgeTrigger* et = dynamic_cast<EdgeTrigger*>(m_trigger);

	string reply = m_transport->SendCommandQueuedWithReply("TRIGGER1:SOURCE?");
	et->SetInput(0, StreamDescriptor(GetChannelByHwName(reply), 0), true);

	reply = m_transport->SendCommandQueuedWithReply("TRIGGER1:EDGE:SLOPE?");
	if (reply == "POS")
		et->SetType(EdgeTrigger::EDGE_RISING);
	else if (reply == "NEG")
		et->SetType(EdgeTrigger::EDGE_FALLING);
	else if (reply == "EITH")
		et->SetType(EdgeTrigger::EDGE_ANY);
	else
	{
		LogWarning("Unknown edge type\n");
		et->SetType(EdgeTrigger::EDGE_ANY);
	}

	reply = m_transport->SendCommandQueuedWithReply("TRIGGER1:LEVEL?");
	et->SetLevel(stof(reply));
}
*/
void SocketCANAnalyzer::PushTrigger()
{
	/*
	auto et = dynamic_cast<EdgeTrigger*>(m_trigger);
	if(et)
		PushEdgeTrigger(et);
	else
		LogWarning("Unknown trigger type (not an edge)\n");*/
}

/**
	@brief Pushes settings for an edge trigger to the instrument
 *//*
void SocketCANAnalyzer::PushEdgeTrigger(EdgeTrigger* trig)
{

	m_transport->SendCommandQueued("TRIGGER1:EVENT SINGLE");
	m_transport->SendCommandQueued("TRIGGER1:TYPE EDGE");
	m_transport->SendCommandQueued(string("TRIGGER1:SOURCE ") + trig->GetInput(0).m_channel->GetHwname());

	switch(trig->GetType())
	{
		case EdgeTrigger::EDGE_RISING:
			m_transport->SendCommandQueued("TRIGGER1:EDGE:SLOPE POSITIVE");
			break;

		case EdgeTrigger::EDGE_FALLING:
			m_transport->SendCommandQueued("TRIGGER1:EDGE:SLOPE NEGATIVE");
			break;

		case EdgeTrigger::EDGE_ANY:
			m_transport->SendCommandQueued("TRIGGER1:EDGE:SLOPE EITHER");
			break;

		default:
			LogWarning("Unknown edge type\n");
			break;
	}
}
*/

#endif
