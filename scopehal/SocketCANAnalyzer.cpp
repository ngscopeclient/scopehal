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

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SocketCANAnalyzer::SocketCANAnalyzer(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)/*
	, m_hasAFG(false)
	, m_triggerArmed(false)
	, m_triggerOneShot(false)
	, m_sampleRateValid(false)
	, m_sampleDepthValid(false)
	, m_triggerOffsetValid(false)*/
{
	/*
	LogDebug("m_model: %s\n", m_model.c_str());
	if (m_model != "RTO6")
	{
		LogFatal("rs.rto6 driver only appropriate for RTO6");
	}

	SCPISocketTransport* sockettransport = NULL;

	if (!(sockettransport = dynamic_cast<SCPISocketTransport*>(transport)))
	{
		LogFatal("rs.rto6 driver requires 'lan' transport");
	}

	// RTO6 always has four analog channels
	m_analogChannelCount = 4;
	for(unsigned int i=0; i<m_analogChannelCount; i++)
	{
		//Hardware name of the channel
		string chname = string("CHAN1");
		chname[4] += i;

		//Color the channels based on R&S's standard color sequence (yellow-green-orange-bluegray)
		string color = "#ffffff";
		switch(i)
		{
			case 0:
				color = "#ffff00";
				break;

			case 1:
				color = "#00ff00";
				break;

			case 2:
				color = "#ff8000";
				break;

			case 3:
				color = "#8080ff";
				break;
		}

		//Create the channel
		auto chan = new OscilloscopeChannel(
			this,
			chname,
			color,
			Unit(Unit::UNIT_FS),
			Unit(Unit::UNIT_VOLTS),
			Stream::STREAM_TYPE_ANALOG,
			i);
		m_channels.push_back(chan);
		chan->SetDefaultDisplayName();
	}

	// All RTO6 have external trigger; only edge is supported
	m_extTrigChannel = new OscilloscopeChannel(
		this,
		"EXT",
		"",
		Unit(Unit::UNIT_FS),
		Unit(Unit::UNIT_VOLTS),
		Stream::STREAM_TYPE_TRIGGER,
		m_channels.size());
	m_channels.push_back(m_extTrigChannel);

	m_digitalChannelBase = m_channels.size();
	m_digitalChannelCount = 0;

	string reply = m_transport->SendCommandQueuedWithReply("*OPT?", false);
	vector<string> opts;
	stringstream s_stream(reply);
	while(s_stream.good()) {
		string substr;
		getline(s_stream, substr, ',');
		opts.push_back(substr);
	}

	for (auto app : opts)
	{
		if (app == "B1")
		{
			LogVerbose(" * RTO6 has logic analyzer/MSO option\n");
			m_digitalChannelCount = 16; // Always 16 (2x8 probe "pods") to my understanding
		}
		else if (app == "B6")
		{
			LogVerbose(" * RTO6 has func gen option\n");
			m_hasAFG = true;
		}
		else
		{
			LogDebug("(* Also has option '%s' (ignored))\n", app.c_str());
		}
	}

	// Set up digital channels (if any)
	for(unsigned int i=0; i<m_digitalChannelCount; i++)
	{
		//Hardware name of the channel
		string chname = string("D0");
		chname[1] += i;

		//Create the channel
		auto chan = new OscilloscopeChannel(
			this,
			chname,
			"#555555",
			Unit(Unit::UNIT_FS),
			Unit(Unit::UNIT_COUNTS),
			Stream::STREAM_TYPE_DIGITAL,
			m_channels.size());
		m_channels.push_back(chan);
		chan->SetDefaultDisplayName();
	}

	if (m_digitalChannelCount)
		m_transport->SendCommandQueued("DIG1:THCoupling OFF"); //Allow different threshold per-bank

	if (m_hasAFG)
	{
		m_transport->SendCommandQueued("WGEN1:SOURCE FUNCGEN"); //Don't currently support modulation or other modes
		m_transport->SendCommandQueued("WGEN2:SOURCE FUNCGEN");

		for (int i = 0; i < 2; i++)
		{
			m_firstAFGIndex = m_channels.size();
			auto ch = new FunctionGeneratorChannel(this, "WGEN" + to_string(i + 1), "#808080", m_channels.size());
			m_channels.push_back(ch);
		}
	}

	m_transport->SendCommandQueued("FORMat:DATA REAL,32"); //Report in f32
	m_transport->SendCommandQueued("ACQuire:COUNt 1"); //Limit to one acquired waveform per "SINGLE"
	m_transport->SendCommandQueued("EXPort:WAVeform:INCXvalues OFF"); //Don't include X values in data
	m_transport->SendCommandQueued("TIMebase:ROLL:ENABle OFF"); //No roll mode
	m_transport->SendCommandQueued("TRIGGER1:MODE NORMAL"); //No auto trigger
	m_transport->SendCommandQueued("ACQuire:CDTA ON"); //All channels have same timebase/etc
	m_transport->SendCommandQueued("PROBE1:SETUP:ATT:MODE MAN"); //Allow/use manual attenuation setting with unknown probes
	m_transport->SendCommandQueued("SYSTEM:KLOCK OFF"); //Don't lock front-panel
	m_transport->SendCommandQueued("*WAI");

	GetSampleDepth();
	*/
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

vector<unsigned int> SocketCANAnalyzer::GetChannelBandwidthLimiters(size_t i)
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

bool SocketCANAnalyzer::AcquireData()
{
	/*
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->FlushCommandQueue();
	LogDebug(" ** AcquireData ** \n");
	LogIndenter li;

	GetSampleDepth();

	auto start_time = std::chrono::system_clock::now();

	// m_transport->SendCommandQueued("*DCL; *WAI");

	map<int, vector<WaveformBase*> > pending_waveforms;
	bool any_data = false;

	for(size_t i=0; i<m_analogChannelCount; i++)
	{
		if(!IsChannelEnabled(i))
			continue;

		LogDebug("Starting acquisition phase for ch%ld\n", i);

		auto cap = new UniformAnalogWaveform;
		size_t length = AcquireHeader(cap, m_channels[i]->GetHwname());

		if (!length)
		{
			delete cap;
			pending_waveforms[i].push_back(NULL);
			continue;
		}

		any_data = true;

		size_t transferred = 0;
		const size_t block_size =
			#if __APPLE__
				50e6 // For some reason values larger than this on my coworkers macbook fail in recv(2)
			#else
				10000e6
			#endif
		;

		unsigned char* dest_buf = (unsigned char*)cap->m_samples.GetCpuPointer();

		LogDebug(" - Begin transfer of %lu bytes\n", length);

		while (transferred != length)
		{
			size_t this_length = block_size;
			if (this_length > (length - transferred))
				this_length = length - transferred;

			string params =  " "+to_string(transferred)+","+to_string(this_length);

			if (transferred == 0 && this_length == length)
				params = "";

			LogDebug("[%3d%%] Query ...`DATA?%s` (B)\n", (int)(100*((float)transferred/(float)length)), params.c_str());

			//Ask for the data
			size_t len_bytes;
			unsigned char* samples = (unsigned char*)m_transport->SendCommandImmediateWithRawBlockReply(m_channels[i]->GetHwname() + ":DATA?"+params+"; *WAI", len_bytes);

			if (len_bytes != (this_length*sizeof(float)))
			{
				LogError("Unexpected number of bytes back; aborting acquisition");
				std::this_thread::sleep_for(std::chrono::microseconds(100000));
				m_transport->FlushRXBuffer();

				delete cap;

				for (auto* c : pending_waveforms[i])
				{
					delete c;
				}

				delete[] samples;

				return false;
			}

			unsigned char* cpy_target = dest_buf+(transferred*sizeof(float));
			// LogDebug("Copying %luB from %p to %p\n", len_bytes, samples, cpy_target);

			memcpy(cpy_target, samples, len_bytes);
			transferred += this_length;
			delete[] samples;

			//Discard trailing newline
			uint8_t disregard;
			m_transport->ReadRawData(1, &disregard);
		}

		LogDebug("[100%%] Done\n");

		cap->MarkSamplesModifiedFromCpu();

		//Done, update the data
		pending_waveforms[i].push_back(cap);
	}

	bool didAcquireAnyDigitalChannels = false;

	for(size_t i=m_digitalChannelBase; i<(m_digitalChannelBase + m_digitalChannelCount); i++)
	{
		if(!IsChannelEnabled(i))
			continue;

		if (!didAcquireAnyDigitalChannels)
		{
			while (m_transport->SendCommandImmediateWithReply("FORM?") != "ASC,0")
			{
				m_transport->SendCommandImmediate("FORM ASC; *WAI"); //Only possible to get data out in ASCII format
				std::this_thread::sleep_for(std::chrono::microseconds(1000000));
			}
			didAcquireAnyDigitalChannels = true;
		}

		string hwname = "DIG" + to_string(HWDigitalNumber(i));

		LogDebug("Starting acquisition for dig%d\n", HWDigitalNumber(i));

		auto cap = new SparseDigitalWaveform;
		size_t length = AcquireHeader(cap, hwname);

		if (!length)
		{
			delete cap;
			pending_waveforms[i].push_back(NULL);
			continue;
		}

		size_t expected_bytes = length * 2; // Commas between items + newline

		// Digital channels do not appear to support selecting a subset, so no 'chunking'

		LogDebug(" - Begin transfer of %lu bytes (*2)\n", length);

		// Since it's ascii the scope just sends it as a SCPI 'line' without the size block
		m_transport->SendCommandImmediate(hwname + ":DATA?; *WAI");
		unsigned char* samples = new unsigned char[expected_bytes];
		size_t read_bytes = m_transport->ReadRawData(expected_bytes, samples);

		if (read_bytes != expected_bytes)
		{
			LogWarning("Unexpected number of bytes back; aborting acquisiton\n");
			std::this_thread::sleep_for(std::chrono::microseconds(100000));
			m_transport->FlushRXBuffer();

			delete cap;

			for (auto* c : pending_waveforms[i])
			{
				delete c;
			}

			delete[] samples;

			return false;
		}

		bool last = samples[0] == '1';

		cap->m_offsets[0] = 0;
		cap->m_durations[0] = 1;
		cap->m_samples[0] = last;

		size_t k = 0;

		for(size_t m=1; m<length; m++)
		{
			bool sample = samples[m*2] == '1';

			//Deduplicate consecutive samples with same value
			//FIXME: temporary workaround for rendering bugs
			//if(last == sample)
			if( (last == sample) && ((m+5) < length) && (m > 5))
				cap->m_durations[k] ++;

			//Nope, it toggled - store the new value
			else
			{
				k++;
				cap->m_offsets[k] = m;
				cap->m_durations[k] = 1;
				cap->m_samples[k] = sample;
				last = sample;
			}
		}

		//Free space reclaimed by deduplication
		cap->Resize(k);
		cap->m_offsets.shrink_to_fit();
		cap->m_durations.shrink_to_fit();
		cap->m_samples.shrink_to_fit();

		cap->MarkSamplesModifiedFromCpu();
		cap->MarkTimestampsModifiedFromCpu();

		delete[] samples;

		//Done, update the data
		pending_waveforms[i].push_back(cap);
	}

	if (didAcquireAnyDigitalChannels)
		m_transport->SendCommandImmediate("FORMat:DATA REAL,32"); //Return to f32

	if (any_data)
	{
		//Now that we have all of the pending waveforms, save them in sets across all channels
		m_pendingWaveformsMutex.lock();
		size_t num_pending = 1;	//TODO: segmented capture support
		for(size_t i=0; i<num_pending; i++)
		{
			SequenceSet s;
			for(size_t j=0; j<m_channels.size(); j++)
			{
				if(IsChannelEnabled(j))
					s[m_channels[j]] = pending_waveforms[j][i];
			}
			m_pendingWaveforms.push_back(s);
		}
		m_pendingWaveformsMutex.unlock();
	}

	if(!any_data || !m_triggerOneShot)
	{
		m_transport->SendCommandImmediate("SINGle");
		std::this_thread::sleep_for(std::chrono::microseconds(100000));
		// If we don't wait here, sending the query for available waveforms will race and return 1 for the exitisting waveform and jam everything up.
		m_triggerArmed = true;
	}
	else
	{
		m_triggerArmed = false;
	}

	auto end_time = std::chrono::system_clock::now();

	LogDebug("Acquisition took %lu\n", std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count());

	return any_data;*/

	return true;
}

void SocketCANAnalyzer::Start()
{/*
	LogDebug("Start\n");
	m_transport->SendCommandImmediate("SINGle");
	std::this_thread::sleep_for(std::chrono::microseconds(100000));
	// If we don't wait here, sending the query for available waveforms will race and return 1 for the exitisting waveform and jam everything up.
	m_triggerArmed = true;
	m_triggerOneShot = false;*/
}

void SocketCANAnalyzer::StartSingleTrigger()
{/*
	LogDebug("Start oneshot\n");
	m_transport->SendCommandImmediate("SINGle");
	std::this_thread::sleep_for(std::chrono::microseconds(100000));
	// If we don't wait here, sending the query for available waveforms will race and return 1 for the exitisting waveform and jam everything up.
	m_triggerArmed = true;
	m_triggerOneShot = true;*/
}

void SocketCANAnalyzer::Stop()
{/*
	m_triggerArmed = false;

	LogDebug("Stop!\n");
	m_transport->SendCommandImmediate("STOP");
	m_triggerArmed = false;
	m_triggerOneShot = true;*/
}

void SocketCANAnalyzer::ForceTrigger()
{
	/*if (m_triggerArmed)
		m_transport->SendCommandImmediate("TRIGGER1:FORCE");*/
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
