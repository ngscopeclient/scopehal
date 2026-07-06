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
	@brief Implementation of AntikernelLabsILA
	@ingroup scopedrivers
 */

#include "scopehal.h"
#include "AntikernelLabsILA.h"
#include "IBM8b10bWaveform.h"
#include "EdgeTrigger.h"
#include <charconv>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initialize the driver_ver

	@param transport	SCPITransport pointing to the debug bridge server
 */
AntikernelLabsILA::AntikernelLabsILA(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, m_triggerArmed(false)
	, m_triggerOneShot(false)
	, m_triggerWordPosition(0)
{
	uint32_t startpos = 0;
	for(size_t i=0; i<32; i++)
	{
		string hwname = string("PROBE") + to_string(i);
		auto name = Trim(m_transport->SendCommandQueuedWithReply(hwname + ":NAME?"));
		auto width = stoi(Trim(m_transport->SendCommandQueuedWithReply(hwname + ":WIDTH?")));
		if(width > 0)
		{
			m_channelWidths.push_back(width);
			m_channelStarts.push_back(startpos);
			startpos += width;

			auto chan = new OscilloscopeChannel(
				this,
				hwname,
				"#00ff00",
				Unit(Unit::UNIT_FS),
				Unit(Unit::UNIT_COUNTS),
				(width > 1) ? Stream::STREAM_TYPE_DIGITAL_BUS : Stream::STREAM_TYPE_DIGITAL,
				m_channels.size());

			chan->SetDigitalWidth(0, width);

			chan->SetDisplayName(name);
			m_channels.push_back(chan);
		}
	}

	m_memDepth = stoul(Trim(m_transport->SendCommandQueuedWithReply("MEM:DEPTH?")));
	m_period = stoull(Trim(m_transport->SendCommandQueuedWithReply("MEM:PERIOD?"))) * 1000;

	m_srate = FS_PER_SECOND / m_period;

	//Set up initial placeholder trigger
	PullTrigger();
}

AntikernelLabsILA::~AntikernelLabsILA()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

///@brief Return the constant driver name
string AntikernelLabsILA::GetDriverNameInternal()
{
	return "akl.ila";
}

unsigned int AntikernelLabsILA::GetInstrumentTypes() const
{
	return Instrument::INST_OSCILLOSCOPE;
}

uint32_t AntikernelLabsILA::GetInstrumentTypesForChannel(size_t /*i*/) const
{
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device interface functions

void AntikernelLabsILA::FlushConfigCache()
{
}

OscilloscopeChannel* AntikernelLabsILA::GetExternalTrigger()
{
	return nullptr;
}

bool AntikernelLabsILA::IsChannelEnabled([[maybe_unused]] size_t i)
{
	return true;
}

void AntikernelLabsILA::EnableChannel(size_t /*i*/)
{
}

void AntikernelLabsILA::DisableChannel(size_t /*i*/)
{
}

vector<OscilloscopeChannel::CouplingType> AntikernelLabsILA::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	return ret;
}

OscilloscopeChannel::CouplingType AntikernelLabsILA::GetChannelCoupling(size_t /*i*/)
{
	return OscilloscopeChannel::COUPLE_DC_50;
}

void AntikernelLabsILA::SetChannelCoupling(size_t /*i*/, OscilloscopeChannel::CouplingType /*type*/)
{
}

double AntikernelLabsILA::GetChannelAttenuation(size_t /*i*/)
{
	return 0;
}

void AntikernelLabsILA::SetChannelAttenuation(size_t /*i*/, double /*atten*/)
{
}

string AntikernelLabsILA::GetProbeName(size_t /*i*/)
{
	return "";
}

bool AntikernelLabsILA::IsDigitalThresholdConfigurable()
{
	return false;
}

unsigned int AntikernelLabsILA::GetChannelBandwidthLimit(size_t /*i*/)
{
	return 0;
}

void AntikernelLabsILA::SetChannelBandwidthLimit(size_t /*i*/, unsigned int /*limit_mhz*/)
{
}

vector<unsigned int> AntikernelLabsILA::GetChannelBandwidthLimiters(size_t /*i*/)
{
	vector<unsigned int> ret;
	return ret;
}

float AntikernelLabsILA::GetChannelVoltageRange(size_t /*i*/, size_t /*stream*/)
{
	return 0;
}

void AntikernelLabsILA::SetChannelVoltageRange(size_t /*i*/, size_t /*stream*/, float /*range*/)
{
}

float AntikernelLabsILA::GetChannelOffset(size_t /*i*/, size_t /*stream*/)
{
	return 0;
}

void AntikernelLabsILA::SetChannelOffset(size_t /*i*/, size_t /*stream*/, float /*offset*/)
{
}

//////////////////////////////////////////////////////////////////////////////// </Digital>

Oscilloscope::TriggerMode AntikernelLabsILA::PollTrigger()
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

bool AntikernelLabsILA::AcquireData()
{
	//Get the data
	auto data = m_transport->SendCommandQueuedWithReply("DATA?");
	auto fields = explode(data, ',');

	double now = GetTime();
	int64_t sec = floor(now);
	int64_t fs = (now - floor(now)) * FS_PER_SECOND;

	//Set up output waveforms
	vector<WaveformBase*> waves;
	map<size_t, UniformDigitalWaveform*> digwaves;
	map<size_t, UniformDigitalBusWaveform32*> bwave32;

	SequenceSet s;
	for(size_t i=0; i<m_channelWidths.size(); i++)
	{
		//Skip multi-bit streams for now
		auto w = m_channelWidths[i];

		if(w == 1)
		{
			//It's a single bit digital waveform if we get here
			auto u = new UniformDigitalWaveform;
			u->m_timescale = m_period;
			u->m_triggerPhase = 0;
			u->m_startTimestamp = sec;
			u->m_startFemtoseconds = fs;
			u->Resize(m_memDepth);
			u->PrepareForCpuAccess();
			u->MarkModifiedFromCpu();

			waves.push_back(u);
			digwaves[i] = u;
			s[m_channels[i]] = u;
		}

		else if(w <= 32)
		{
			auto u = new UniformDigitalBusWaveform32;
			u->m_timescale = m_period;
			u->m_triggerPhase = 0;
			u->m_startTimestamp = sec;
			u->m_startFemtoseconds = fs;
			u->Resize(m_memDepth);
			u->PrepareForCpuAccess();
			u->MarkModifiedFromCpu();

			waves.push_back(u);
			bwave32[i] = u;
			s[m_channels[i]] = u;
		}

		else
			waves.push_back(nullptr);
	}

	//Unpack each sample's data
	vector<uint8_t> row;
	size_t nbytes = fields[0].length() / 2;
	row.resize(nbytes);
	for(size_t i=0; i<m_memDepth; i++)
	{
		//Convert the hex data into binary and invert the byte ordering so LSB is in position 0
		auto p = fields[i].c_str();
		for(size_t j=0; j<nbytes; j++)
			from_chars(p + j*2, p + j*2 + 2, row[nbytes - j - 1], 16);

		for(size_t j=0; j<m_channelWidths.size(); j++)
		{
			auto dw = digwaves[j];
			auto d32 = bwave32[j];
			auto width = m_channelWidths[j];

			auto bitstart = m_channelStarts[j];
			auto bytestart = bitstart / 8;
			auto bitpos = bitstart % 8;

			//Single bit signal
			if(dw)
			{
				if( (row[bytestart] >> bitpos) & 1)
					dw->m_samples[i] = true;
			}

			//Vector signal of <= 32 bits
			else if(d32)
			{
				//TODO make this more efficient and not copy a bit at a time
				uint32_t tmp = 0;
				for(size_t k=0; k<width; k++)
				{
					if( (row[bytestart] >> bitpos) & 1)
						tmp |= (1 << k);

					//bump bit position
					bitpos ++;
					if(bitpos >= 8)
					{
						bytestart ++;
						bitpos -= 8;
					}
				}

				d32->m_samples[i] = tmp;
			}

			else
				continue;
		}
	}

	//Save newly created waveform
	m_pendingWaveformsMutex.lock();
		m_pendingWaveforms.push_back(s);
	m_pendingWaveformsMutex.unlock();

	//Re-arm the trigger if needed
	if(m_triggerOneShot)
		m_triggerArmed = false;
	else
		m_transport->SendCommandQueued("TRIG:ARM");

	return true;
}

void AntikernelLabsILA::Start()
{
	m_triggerArmed = true;
	m_triggerOneShot = false;
	m_transport->SendCommand("TRIG:ARM");
}

void AntikernelLabsILA::StartSingleTrigger()
{
	m_triggerArmed = true;
	m_triggerOneShot = true;
	m_transport->SendCommand("TRIG:ARM");
}

void AntikernelLabsILA::Stop()
{
	m_triggerArmed = false;
	m_triggerOneShot = true;
	m_transport->SendCommand("TRIG:STOP");
}

void AntikernelLabsILA::ForceTrigger()
{
	StartSingleTrigger();
}

bool AntikernelLabsILA::IsTriggerArmed()
{
	return m_triggerArmed;
}

vector<uint64_t> AntikernelLabsILA::GetSampleRatesNonInterleaved()
{
	vector<uint64_t> ret;
	ret.push_back(m_srate);
	return ret;
}

vector<uint64_t> AntikernelLabsILA::GetSampleRatesInterleaved()
{
	return GetSampleRatesNonInterleaved();
}

set<Oscilloscope::InterleaveConflict> AntikernelLabsILA::GetInterleaveConflicts()
{
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> AntikernelLabsILA::GetSampleDepthsNonInterleaved()
{
	vector<uint64_t> ret;
	ret.push_back(m_memDepth);
	return ret;
}

vector<uint64_t> AntikernelLabsILA::GetSampleDepthsInterleaved()
{
	return GetSampleRatesNonInterleaved();
}

bool AntikernelLabsILA::HasInterleavingControls()
{
	return false;
}

uint64_t AntikernelLabsILA::GetSampleRate()
{
	return m_srate;
}

uint64_t AntikernelLabsILA::GetSampleDepth()
{
	return m_memDepth;
}

void AntikernelLabsILA::SetSampleDepth(uint64_t /*depth*/)
{
}

void AntikernelLabsILA::SetSampleRate(uint64_t /*rate*/)
{

}

void AntikernelLabsILA::SetTriggerOffset(int64_t offset)
{
	int64_t idx = offset / m_period;
	idx = max(idx, (int64_t) 0);
	m_triggerWordPosition = idx;
	m_triggerWordPosition = min(m_triggerWordPosition, m_memDepth - 1);

	m_transport->SendCommandQueued(string("TRIG:POS ") + to_string(m_triggerWordPosition));
}

int64_t AntikernelLabsILA::GetTriggerOffset()
{
	LogTrace("Trigger offset %u\n", m_triggerWordPosition);

	return m_triggerWordPosition * m_period;
}

bool AntikernelLabsILA::IsInterleaving()
{
	return false;
}

bool AntikernelLabsILA::SetInterleaving(bool /*combine*/)
{
	return false;
}

void AntikernelLabsILA::PullTrigger()
{
	//Clear out any triggers of the wrong type
	if( (m_trigger != nullptr) && (dynamic_cast<EdgeTrigger*>(m_trigger) != nullptr) )
	{
		delete m_trigger;
		m_trigger = nullptr;
	}

	//Create a new trigger if necessary
	auto trig = dynamic_cast<EdgeTrigger*>(m_trigger);
	if(trig == nullptr)
	{
		trig = new EdgeTrigger(this);
		m_trigger = trig;
	}

	//Set the input
	m_trigger->SetInput(0, StreamDescriptor(m_channels[0], 0));

	//Get trigger position
	m_triggerWordPosition = stoi(m_transport->SendCommandQueuedWithReply("TRIG:POS?"));
}

void AntikernelLabsILA::PushTrigger()
{
}
