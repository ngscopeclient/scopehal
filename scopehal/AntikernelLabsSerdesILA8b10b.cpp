/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Implementation of AntikernelLabsSerdesILA8b10b
	@ingroup scopedrivers
 */

#include "scopehal.h"
#include "AntikernelLabsSerdesILA8b10b.h"
#include "IBM8b10bWaveform.h"
#include "EdgeTrigger.h"
#include "CDR8B10BTrigger.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initialize the driver_ver

	@param transport	SCPITransport pointing to the debug bridge server
 */
AntikernelLabsSerdesILA8b10b::AntikernelLabsSerdesILA8b10b(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, m_triggerArmed(false)
	, m_triggerOneShot(false)
	, m_triggerWordPosition(0)
{
	auto chan = new OscilloscopeChannel(
		this,
		"data",
		"#ffff00",
		Unit(Unit::UNIT_FS),
		Unit(Unit::UNIT_COUNTS),
		Stream::STREAM_TYPE_PROTOCOL,
		0);
	m_channels.push_back(chan);
	chan->SetDefaultDisplayName();

	m_memDepth = stoul(Trim(m_transport->SendCommandQueuedWithReply("MEM:DEPTH?")));
	m_period = stoull(Trim(m_transport->SendCommandQueuedWithReply("MEM:PERIOD?"))) * 1000;

	m_srate = FS_PER_SECOND / m_period;

	//Set up initial placeholder trigger
	PullTrigger();
}

AntikernelLabsSerdesILA8b10b::~AntikernelLabsSerdesILA8b10b()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

///@brief Return the constant driver name
string AntikernelLabsSerdesILA8b10b::GetDriverNameInternal()
{
	return "akl.ila.8b10b";
}

unsigned int AntikernelLabsSerdesILA8b10b::GetInstrumentTypes() const
{
	return Instrument::INST_OSCILLOSCOPE;
}

uint32_t AntikernelLabsSerdesILA8b10b::GetInstrumentTypesForChannel(size_t /*i*/) const
{
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device interface functions

void AntikernelLabsSerdesILA8b10b::FlushConfigCache()
{
}

OscilloscopeChannel* AntikernelLabsSerdesILA8b10b::GetExternalTrigger()
{
	return nullptr;
}

bool AntikernelLabsSerdesILA8b10b::IsChannelEnabled(size_t i)
{
	return (i == 0);
}

void AntikernelLabsSerdesILA8b10b::EnableChannel(size_t /*i*/)
{
}

void AntikernelLabsSerdesILA8b10b::DisableChannel(size_t /*i*/)
{
}

vector<OscilloscopeChannel::CouplingType> AntikernelLabsSerdesILA8b10b::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	return ret;
}

OscilloscopeChannel::CouplingType AntikernelLabsSerdesILA8b10b::GetChannelCoupling(size_t /*i*/)
{
	return OscilloscopeChannel::COUPLE_DC_50;
}

void AntikernelLabsSerdesILA8b10b::SetChannelCoupling(size_t /*i*/, OscilloscopeChannel::CouplingType /*type*/)
{
}

double AntikernelLabsSerdesILA8b10b::GetChannelAttenuation(size_t /*i*/)
{
	return 0;
}

void AntikernelLabsSerdesILA8b10b::SetChannelAttenuation(size_t /*i*/, double /*atten*/)
{
}

string AntikernelLabsSerdesILA8b10b::GetProbeName(size_t /*i*/)
{
	return "";
}

unsigned int AntikernelLabsSerdesILA8b10b::GetChannelBandwidthLimit(size_t /*i*/)
{
	return 0;
}

void AntikernelLabsSerdesILA8b10b::SetChannelBandwidthLimit(size_t /*i*/, unsigned int /*limit_mhz*/)
{
}

vector<unsigned int> AntikernelLabsSerdesILA8b10b::GetChannelBandwidthLimiters(size_t /*i*/)
{
	vector<unsigned int> ret;
	return ret;
}

float AntikernelLabsSerdesILA8b10b::GetChannelVoltageRange(size_t /*i*/, size_t /*stream*/)
{
	return 0;
}

void AntikernelLabsSerdesILA8b10b::SetChannelVoltageRange(size_t /*i*/, size_t /*stream*/, float /*range*/)
{
}

float AntikernelLabsSerdesILA8b10b::GetChannelOffset(size_t /*i*/, size_t /*stream*/)
{
	return 0;
}

void AntikernelLabsSerdesILA8b10b::SetChannelOffset(size_t /*i*/, size_t /*stream*/, float /*offset*/)
{
}

//////////////////////////////////////////////////////////////////////////////// </Digital>

Oscilloscope::TriggerMode AntikernelLabsSerdesILA8b10b::PollTrigger()
{
	if (!m_triggerArmed)
		return TRIGGER_MODE_STOP;

	auto reply = Trim(m_transport->SendCommandQueuedWithReply("TRIG:STAT?"));
	if(reply == "READY")
		return TRIGGER_MODE_TRIGGERED;
	else if(reply == "STOP")
		return TRIGGER_MODE_STOP;
	else
		return TRIGGER_MODE_RUN;
}

bool AntikernelLabsSerdesILA8b10b::AcquireData()
{
	//TODO: fine adjust trigger phase so it points to the actual symbol that it starts at

	double now = GetTime();

	//Make the output waveform
	auto cap = new IBM8b10bWaveform;
	cap->m_timescale = m_period;
	cap->m_triggerPhase = 0;
	cap->m_startTimestamp = floor(now);
	cap->m_startFemtoseconds = (now - floor(now)) * FS_PER_SECOND;
	cap->Resize(m_memDepth);
	cap->PrepareForCpuAccess();

	//Get the data
	auto data = Trim(m_transport->SendCommandQueuedWithReply("DATA?"));
	auto fields = explode(data, ',');

	//Unpack the fields into 8b10b symbols
	for(size_t i=0; i<fields.size(); i++)
	{
		//Crack the word out into 8b10b fields
		uint64_t w = stoull(fields[i], nullptr, 16);

		for(size_t j=0; j<4; j++)
		{
			//Swap adjacent symbol pairs to correct endianness issue
			auto rj = j ^ 1;

			bool data_is_ctl = ((w >> (32 + rj)) & 1) != 0;
			bool symbol_err = ((w >> (36 + rj)) & 1) != 0;
			bool disparity_err = ((w >> (40 + rj)) & 1) != 0;
			bool disparity = ((w >> (44 + rj)) & 1) != 0;
			uint8_t wdata = (w >> (8 * rj)) & 0xff;

			auto idx = i*4 + j;

			//we dont support uniform 8b10b waveforms currently so fake it
			cap->m_offsets[idx] = idx;
			cap->m_durations[idx] = 1;
			cap->m_samples[idx] = IBM8b10bSymbol(
				data_is_ctl,
				symbol_err,
				0,
				disparity_err,
				wdata,
				disparity);
		}
	}

	cap->MarkModifiedFromCpu();

	//Save newly created waveform
	m_pendingWaveformsMutex.lock();
		SequenceSet s;
		s[m_channels[0]] = cap;
		m_pendingWaveforms.push_back(s);
	m_pendingWaveformsMutex.unlock();

	//Re-arm the trigger if needed
	if(m_triggerOneShot)
		m_triggerArmed = false;
	else
		m_transport->SendCommandQueued("TRIG:ARM");

	return true;
}

void AntikernelLabsSerdesILA8b10b::Start()
{
	m_triggerArmed = true;
	m_triggerOneShot = false;
	m_transport->SendCommand("TRIG:ARM");
}

void AntikernelLabsSerdesILA8b10b::StartSingleTrigger()
{
	m_triggerArmed = true;
	m_triggerOneShot = true;
	m_transport->SendCommand("TRIG:ARM");
}

void AntikernelLabsSerdesILA8b10b::Stop()
{
	m_triggerArmed = false;
	m_triggerOneShot = true;
	m_transport->SendCommand("TRIG:STOP");
}

void AntikernelLabsSerdesILA8b10b::ForceTrigger()
{
	StartSingleTrigger();
}

bool AntikernelLabsSerdesILA8b10b::IsTriggerArmed()
{
	return m_triggerArmed;
}

vector<uint64_t> AntikernelLabsSerdesILA8b10b::GetSampleRatesNonInterleaved()
{
	vector<uint64_t> ret;
	ret.push_back(m_srate);
	return ret;
}

vector<uint64_t> AntikernelLabsSerdesILA8b10b::GetSampleRatesInterleaved()
{
	return GetSampleRatesNonInterleaved();
}

set<Oscilloscope::InterleaveConflict> AntikernelLabsSerdesILA8b10b::GetInterleaveConflicts()
{
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> AntikernelLabsSerdesILA8b10b::GetSampleDepthsNonInterleaved()
{
	vector<uint64_t> ret;
	ret.push_back(m_memDepth);
	return ret;
}

vector<uint64_t> AntikernelLabsSerdesILA8b10b::GetSampleDepthsInterleaved()
{
	return GetSampleRatesNonInterleaved();
}

uint64_t AntikernelLabsSerdesILA8b10b::GetSampleRate()
{
	return m_srate;
}

uint64_t AntikernelLabsSerdesILA8b10b::GetSampleDepth()
{
	return m_memDepth;
}

void AntikernelLabsSerdesILA8b10b::SetSampleDepth(uint64_t /*depth*/)
{
}

void AntikernelLabsSerdesILA8b10b::SetSampleRate(uint64_t /*rate*/)
{

}

void AntikernelLabsSerdesILA8b10b::SetTriggerOffset(int64_t offset)
{
	uint32_t numWords = m_memDepth / 4;

	int64_t idx = offset / (4 * m_period);
	idx = max(idx, (int64_t) 0);
	m_triggerWordPosition = idx;
	m_triggerWordPosition = min(m_triggerWordPosition, numWords - 1);

	m_transport->SendCommandQueued(string("TRIG:POS ") + to_string(m_triggerWordPosition));
}

int64_t AntikernelLabsSerdesILA8b10b::GetTriggerOffset()
{
	LogTrace("Trigger offset %u\n", m_triggerWordPosition);

	return m_triggerWordPosition * 4 * m_period;
}

bool AntikernelLabsSerdesILA8b10b::IsInterleaving()
{
	return false;
}

bool AntikernelLabsSerdesILA8b10b::SetInterleaving(bool /*combine*/)
{
	return false;
}

void AntikernelLabsSerdesILA8b10b::PullTrigger()
{
	//for now assume CDR trigger just so we have something

	//Clear out any triggers of the wrong type
	if( (m_trigger != nullptr) && (dynamic_cast<CDR8B10BTrigger*>(m_trigger) != nullptr) )
	{
		delete m_trigger;
		m_trigger = nullptr;
	}

	//Create a new trigger if necessary
	auto trig = dynamic_cast<CDR8B10BTrigger*>(m_trigger);
	if(trig == nullptr)
	{
		trig = new CDR8B10BTrigger(this);
		m_trigger = trig;
	}

	//Set the input
	m_trigger->SetInput(0, StreamDescriptor(m_channels[0], 0));

	//Get trigger position
	m_triggerWordPosition = stoi(m_transport->SendCommandQueuedWithReply("TRIG:POS?"));
}

void AntikernelLabsSerdesILA8b10b::PushTrigger()
{
}
