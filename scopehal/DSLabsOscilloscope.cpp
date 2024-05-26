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

#ifdef _WIN32
#include <chrono>
#include <thread>
#endif

#include "scopehal.h"
#include "DSLabsOscilloscope.h"
#include "EdgeTrigger.h"

using namespace std;

#define RATE_5GSPS		(5000L * 1000L * 1000L)
#define RATE_2P5GSPS	(2500L * 1000L * 1000L)
#define RATE_1P25GSPS	(1250L * 1000L * 1000L)
#define RATE_625MSPS	(625L * 1000L * 1000L)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Construction / destruction

DSLabsOscilloscope::DSLabsOscilloscope(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, RemoteBridgeOscilloscope(transport, true)
	, m_diag_hardwareWFMHz(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ))
	, m_diag_receivedWFMHz(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ))
	, m_diag_totalWFMs(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS))
	, m_diag_droppedWFMs(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS))
	, m_diag_droppedPercent(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_PERCENT))
{
	//Set up initial cache configuration as "not valid" and let it populate as we go
	IdentifyHardware();

	AddDiagnosticLog("Found Model: " + m_model);

	//Add analog channel objects
	for(size_t i = 0; i < m_analogChannelCount; i++)
	{
		//Hardware name of the channel
		string chname = to_string(i);

		//Create the channel
		auto chan = new OscilloscopeChannel(
			this,
			chname,
			GetChannelColor(i),
			Unit(Unit::UNIT_FS),
			Unit(Unit::UNIT_VOLTS),
			Stream::STREAM_TYPE_ANALOG,
			i);
		m_channels.push_back(chan);

		string nicename = "ch" + chname;
		chan->SetDisplayName(nicename);

		//Set initial configuration so we have a well-defined instrument state
		m_channelAttenuations[i] = 10;
		SetChannelCoupling(i, OscilloscopeChannel::COUPLE_AC_1M);
		SetChannelOffset(i, 0,  0);
		SetChannelVoltageRange(i, 0, 5);
	}

	//Add digital channel objects
	for(size_t i = 0; i < m_digitalChannelCount; i++)
	{
		//Hardware name of the channel
		size_t chnum = m_digitalChannelBase + i;
		string chname = to_string(chnum);

		//Create the channel
		auto chan = new OscilloscopeChannel(
			this,
			chname,
			GetChannelColor(i),
			Unit(Unit::UNIT_FS),
			Unit(Unit::UNIT_COUNTS),
			Stream::STREAM_TYPE_DIGITAL,
			chnum);
		m_channels.push_back(chan);

		string nicename = "d" + to_string(i);
		chan->SetDisplayName(nicename);

		SetDigitalHysteresis(chnum, 0.1);
		SetDigitalThreshold(chnum, 0.1);
	}

	//Set initial memory configuration.
	SetSampleRate(1000000L);
	SetSampleDepth(10000);

	//Set up the data plane socket
	auto csock = dynamic_cast<SCPITwinLanTransport*>(m_transport);
	if(!csock)
		LogFatal("DSLabsOscilloscope expects a SCPITwinLanTransport\n");

	//Configure the trigger
	auto trig = new EdgeTrigger(this);
	trig->SetType(EdgeTrigger::EDGE_RISING);
	trig->SetLevel(0);
	trig->SetInput(0, StreamDescriptor(GetOscilloscopeChannel(0)));
	SetTrigger(trig);
	PushTrigger();
	SetTriggerOffset(1000000000000); //1ms to allow trigphase interpolation

	m_diagnosticValues["Hardware WFM/s"] = &m_diag_hardwareWFMHz;
	m_diagnosticValues["Received WFM/s"] = &m_diag_receivedWFMHz;
	m_diagnosticValues["Total Waveforms Received"] = &m_diag_totalWFMs;
	m_diagnosticValues["Received Waveforms Dropped"] = &m_diag_droppedWFMs;
	m_diagnosticValues["% Received Waveforms Dropped"] = &m_diag_droppedPercent;

	ResetPerCaptureDiagnostics();
}

void DSLabsOscilloscope::ResetPerCaptureDiagnostics()
{
	m_diag_hardwareWFMHz.SetFloatVal(0);
	m_diag_receivedWFMHz.SetFloatVal(0);
	m_diag_totalWFMs.SetIntVal(0);
	m_diag_droppedWFMs.SetIntVal(0);
	m_diag_droppedPercent.SetFloatVal(1);
	m_receiveClock.Reset();
}

/**
	@brief Color the channels based on Pico's standard color sequence (blue-red-green-yellow-purple-gray-cyan-magenta)
 */
string DSLabsOscilloscope::GetChannelColor(size_t i)
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

void DSLabsOscilloscope::IdentifyHardware()
{
	//Assume no MSO channels to start
	m_analogChannelCount = 0;
	m_digitalChannelBase = 0;
	m_digitalChannelCount = 0;

	m_series = SERIES_UNKNOWN;

	if (m_model == "DSCope U3P100")
	{
		m_series = DSCOPE_U3P100;
		LogDebug("Found DSCope U3P100\n");

		m_analogChannelCount = 2;
		m_digitalChannelCount = 0;

	}
	else if (m_model == "DSLogic U3Pro16")
	{
		m_series = DSLOGIC_U3PRO16;
		LogDebug("Found DSLogic U3Pro16\n");

		m_analogChannelCount = 0;
		m_digitalChannelCount = 16;

	}

	if (m_series == SERIES_UNKNOWN)
		LogWarning("Unknown DSLabs model \"%s\"\n", m_model.c_str());
}

DSLabsOscilloscope::~DSLabsOscilloscope()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Accessors

unsigned int DSLabsOscilloscope::GetInstrumentTypes() const
{
	return Instrument::INST_OSCILLOSCOPE;
}

uint32_t DSLabsOscilloscope::GetInstrumentTypesForChannel(size_t /*i*/) const
{
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Device interface functions

string DSLabsOscilloscope::GetDriverNameInternal()
{
	return "dslabs";
}

void DSLabsOscilloscope::FlushConfigCache()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
}

double DSLabsOscilloscope::GetChannelAttenuation(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelAttenuations[i];
}

void DSLabsOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	double oldAtten = m_channelAttenuations[i];
	m_channelAttenuations[i] = atten;

	//Rescale channel voltage range and offset
	double delta = atten / oldAtten;
	m_channelVoltageRanges[i] *= delta;
	m_channelOffsets[i] *= delta;
}

unsigned int DSLabsOscilloscope::GetChannelBandwidthLimit(size_t /*i*/)
{
	return 0;
}

void DSLabsOscilloscope::SetChannelBandwidthLimit(size_t /*i*/, unsigned int /*limit_mhz*/)
{
}

OscilloscopeChannel* DSLabsOscilloscope::GetExternalTrigger()
{
	//FIXME
	return NULL;
}

Oscilloscope::TriggerMode DSLabsOscilloscope::PollTrigger()
{
	//Always report "triggered" so we can block on AcquireData() in ScopeThread
	//TODO: peek function of some sort?
	return TRIGGER_MODE_TRIGGERED;
}

bool DSLabsOscilloscope::AcquireData()
{
	const uint8_t r = 'K';
	m_transport->SendRawData(1, &r);

	//Read the sequence number of the current waveform
	uint32_t seqnum;
	if(!m_transport->ReadRawData(sizeof(seqnum), (uint8_t*)&seqnum))
		return false;

	//Read the number of channels in the current waveform
	uint16_t numChannels;
	if(!m_transport->ReadRawData(sizeof(numChannels), (uint8_t*)&numChannels))
		return false;

	//Get the sample interval.
	//May be different from m_srate if we changed the rate after the trigger was armed
	int64_t fs_per_sample;
	if(!m_transport->ReadRawData(sizeof(fs_per_sample), (uint8_t*)&fs_per_sample))
		return false;

	//Get the de-facto trigger position.
	int64_t trigger_fs;
	if(!m_transport->ReadRawData(sizeof(trigger_fs), (uint8_t*)&trigger_fs))
		return false;

	{
		lock_guard<recursive_mutex> lock(m_mutex);
		if (m_triggerOffset != trigger_fs)
		{
			AddDiagnosticLog("Correcting trigger offset by " + to_string(m_triggerOffset - trigger_fs));
			m_triggerOffset = trigger_fs;
		}
	}

	//Get the de-facto hardware capture rate.
	double wfms_s;
	if(!m_transport->ReadRawData(sizeof(wfms_s), (uint8_t*)&wfms_s))
		return false;

	m_diag_hardwareWFMHz.SetFloatVal(wfms_s);

	// AddDiagnosticLog("Something... " + to_string(GetTime()));

	// LogDebug("Receive header: SEQ#%u, %d channels\n", seqnum, numChannels);

	//Acquire data for each channel
	size_t chnum;
	size_t memdepth;
	float config[3];
	SequenceSet s;
	double t = GetTime();
	int64_t fs = (t - floor(t)) * FS_PER_SECOND;

	//Analog channels get processed separately
	vector<uint8_t*> abufs;
	vector<UniformAnalogWaveform*> awfms;
	vector<float> scales;
	vector<float> offsets;

	for(size_t i=0; i<numChannels; i++)
	{
		//Get channel ID and memory depth (samples, not bytes)
		if(!m_transport->ReadRawData(sizeof(chnum), (uint8_t*)&chnum))
			return false;
		if(!m_transport->ReadRawData(sizeof(memdepth), (uint8_t*)&memdepth))
			return false;

		// LogDebug("ch%ld: Receive %ld samples\n", chnum, memdepth);

		uint8_t* buf = new uint8_t[memdepth];

		//Analog channels
		if(chnum < m_analogChannelCount)
		{
			abufs.push_back(buf);

			//Scale and offset are sent in the header since they might have changed since the capture began
			if(!m_transport->ReadRawData(sizeof(config), (uint8_t*)&config))
				return false;
			float scale = config[0];
			float offset = config[1];
			float trigphase = -config[2] * fs_per_sample;
			scale *= GetChannelAttenuation(chnum);
			offset *= GetChannelAttenuation(chnum);

			bool clipping;
			if(!m_transport->ReadRawData(sizeof(clipping), (uint8_t*)&clipping))
				return false;

			//TODO: stream timestamp from the server

			if(!m_transport->ReadRawData(memdepth * sizeof(int8_t), (uint8_t*)buf))
				return false;

			//Create our waveform
			auto cap = new UniformAnalogWaveform;
			cap->m_timescale = fs_per_sample;
			cap->m_triggerPhase = trigphase;
			cap->m_startTimestamp = time(NULL);
			cap->m_startFemtoseconds = fs;
			if (clipping)
				cap->m_flags |= WaveformBase::WAVEFORM_CLIPPING;

			cap->Resize(memdepth);
			awfms.push_back(cap);
			scales.push_back(scale);
			offsets.push_back(offset);

			s[GetOscilloscopeChannel(chnum)] = cap;
		}
		else
		{
			int32_t first_sample;

			if(!m_transport->ReadRawData(sizeof(first_sample), (uint8_t*)&first_sample))
				return false;

			if(!m_transport->ReadRawData(memdepth * sizeof(uint8_t), (uint8_t*)buf))
				return false;

			//Create buffers for output waveforms
			auto cap = new SparseDigitalWaveform;
			s[GetOscilloscopeChannel(chnum)] = cap;
			cap->m_timescale = fs_per_sample;
			cap->m_triggerPhase = 0;
			cap->m_startTimestamp = time(NULL);
			cap->m_startFemtoseconds = fs;
			cap->PrepareForCpuAccess();

			//Preallocate memory assuming no deduplication possible
			cap->Resize(memdepth * 8);

			//First sample never gets deduplicated
			bool last = (buf[0] & 1) ? true : false;
			size_t k = 0;
			cap->m_offsets[0] = first_sample;
			cap->m_durations[0] = 1;
			cap->m_samples[0] = last;

			//Read and de-duplicate the other samples
			//TODO: can we vectorize this somehow?
			for(size_t m=1; m<memdepth; m++)
			{
				for (int bit = 0; bit < 8; bit++)
				{
					bool sample = (buf[m] & (1 << bit)) ? true : false;

					//Deduplicate consecutive samples with same value
					//FIXME: temporary workaround for rendering bugs
					//if(last == sample)
					if( (last == sample) && ((m+1) < memdepth) && (m > 0))
						cap->m_durations[k] ++;

					//Nope, it toggled - store the new value
					else
					{
						k++;
						cap->m_offsets[k] = first_sample + (m * 8) + bit;
						cap->m_durations[k] = 1;
						cap->m_samples[k] = sample;
						last = sample;
					}
				}
			}

			//Free space reclaimed by deduplication
			cap->Resize(k);
			cap->m_offsets.shrink_to_fit();
			cap->m_durations.shrink_to_fit();
			cap->m_samples.shrink_to_fit();
			cap->MarkSamplesModifiedFromCpu();
			cap->MarkTimestampsModifiedFromCpu();

			delete[] buf;
		}
	}

	//Process analog captures in parallel
	#pragma omp parallel for
	for(size_t i=0; i<awfms.size(); i++)
	{
		auto cap = awfms[i];
		cap->PrepareForCpuAccess();
		ConvertUnsigned8BitSamples(
			cap->m_samples.GetCpuPointer(),
			abufs[i],
			scales[i],
			offsets[i],
			cap->size());
		cap->MarkSamplesModifiedFromCpu();
		delete[] abufs[i];
	}

	FilterParameter* param = &m_diag_totalWFMs;
	int total = param->GetIntVal() + 1;
	param->SetIntVal(total);

	param = &m_diag_droppedWFMs;
	int dropped = param->GetIntVal();

	//Save the waveforms to our queue
	m_pendingWaveformsMutex.lock();
	m_pendingWaveforms.push_back(s);

	while (m_pendingWaveforms.size() > 2)
	{
		SequenceSet set = *m_pendingWaveforms.begin();
		for(auto it : set)
			delete it.second;
		m_pendingWaveforms.pop_front();

		dropped++;
	}

	m_pendingWaveformsMutex.unlock();

	param->SetIntVal(dropped);

	param = &m_diag_droppedPercent;
	param->SetFloatVal((float)dropped / (float)total);

	m_receiveClock.Tick();
	m_diag_receivedWFMHz.SetFloatVal(m_receiveClock.GetAverageHz());

	//If this was a one-shot trigger we're no longer armed
	if(m_triggerOneShot)
		m_triggerArmed = false;

	return true;
}

void DSLabsOscilloscope::Start()
{
	RemoteBridgeOscilloscope::Start();
	ResetPerCaptureDiagnostics();
}

void DSLabsOscilloscope::StartSingleTrigger()
{
	RemoteBridgeOscilloscope::StartSingleTrigger();
	ResetPerCaptureDiagnostics();
}

void DSLabsOscilloscope::ForceTrigger()
{
	RemoteBridgeOscilloscope::ForceTrigger();
	ResetPerCaptureDiagnostics();
}

vector<uint64_t> DSLabsOscilloscope::GetSampleRatesNonInterleaved()
{
	vector<uint64_t> ret;

	string rates;
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand("RATES?");
		rates = m_transport->ReadReply();
	}

	size_t i=0;
	while(true)
	{
		size_t istart = i;
		i = rates.find(',', i+1);
		if(i == string::npos)
			break;

		auto block = rates.substr(istart, i-istart);
		auto fs = stol(block);
		auto hz = FS_PER_SECOND / fs;
		ret.push_back(hz);

		//skip the comma
		i++;
	}

	return ret;
}

vector<uint64_t> DSLabsOscilloscope::GetSampleRatesInterleaved()
{
	//interleaving not supported
	vector<uint64_t> ret = {};
	return ret;
}

set<Oscilloscope::InterleaveConflict> DSLabsOscilloscope::GetInterleaveConflicts()
{
	// TODO: Need to correctly report that the max ch0 + ch1 sample rate is 500MS/s
	//       whereas the maximum ch0-only sample rate is 1GS/s. This appears to be
	//       the only interleaving conflict that needs expressing.

	//interleaving not supported
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> DSLabsOscilloscope::GetSampleDepthsNonInterleaved()
{
	vector<uint64_t> ret;

	// TODO: More principled way of reporting this. It Seems to cap out at 8MS
	//       for one channel and less for two. Experimentation is needed to
	//       determine if this is a hardware limitation or not (datasheet claims
	//       "2MS single channel" realtime and "256MS single capture" -- does
	//       this mean 256MS equivalent-time?)

	string depths;
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand("DEPTHS?");
		depths = m_transport->ReadReply();
	}

	size_t i=0;
	while(true)
	{
		size_t istart = i;
		i = depths.find(',', i+1);
		if(i == string::npos)
			break;

		ret.push_back(stol(depths.substr(istart, i-istart)));

		//skip the comma
		i++;
	}

	return ret;
}

vector<uint64_t> DSLabsOscilloscope::GetSampleDepthsInterleaved()
{
	//interleaving not supported
	vector<uint64_t> ret;
	return ret;
}

bool DSLabsOscilloscope::IsInterleaving()
{
	//interleaving is done automatically in hardware based on sample rate, no user facing switch for it
	return false;
}

bool DSLabsOscilloscope::SetInterleaving(bool /*combine*/)
{
	//interleaving is done automatically in hardware based on sample rate, no user facing switch for it
	return false;
}

vector<OscilloscopeChannel::CouplingType> DSLabsOscilloscope::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	ret.push_back(OscilloscopeChannel::COUPLE_DC_1M);
	ret.push_back(OscilloscopeChannel::COUPLE_AC_1M);
	return ret;
}

vector<Oscilloscope::AnalogBank> DSLabsOscilloscope::GetAnalogBanks()
{
	vector<AnalogBank> banks;
	banks.push_back(GetAnalogBank(0));
	return banks;
}

Oscilloscope::AnalogBank DSLabsOscilloscope::GetAnalogBank(size_t /*channel*/)
{
	AnalogBank bank;
	return bank;
}

bool DSLabsOscilloscope::IsADCModeConfigurable()
{
	return false;
}

vector<string> DSLabsOscilloscope::GetADCModeNames(size_t /*channel*/)
{
	//All scopes with variable resolution start at 8 bit and go up from there
	vector<string> ret;
	ret.push_back("8 Bit");
	return ret;
}

size_t DSLabsOscilloscope::GetADCMode(size_t /*channel*/)
{
	return 0;
}

void DSLabsOscilloscope::SetADCMode(size_t /*channel*/, size_t /*mode*/)
{
	return;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Logic analyzer configuration

vector<Oscilloscope::DigitalBank> DSLabsOscilloscope::GetDigitalBanks()
{
	vector<DigitalBank> banks;
	return banks;
}

Oscilloscope::DigitalBank DSLabsOscilloscope::GetDigitalBank(size_t /*channel*/)
{
	DigitalBank ret;
	return ret;
}

bool DSLabsOscilloscope::IsDigitalHysteresisConfigurable()
{
	return false;
}

bool DSLabsOscilloscope::IsDigitalThresholdConfigurable()
{
	return true;
}

float DSLabsOscilloscope::GetDigitalHysteresis(size_t /*channel*/)
{
	return 0;
}

float DSLabsOscilloscope::GetDigitalThreshold(size_t /*channel*/)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_digitalThreshold;
}

void DSLabsOscilloscope::SetDigitalHysteresis(size_t /*channel*/, float /*level*/)
{
	// TODO
}

void DSLabsOscilloscope::SetDigitalThreshold(size_t /*channel*/, float level)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		if (m_digitalThreshold == level) return;

		m_digitalThreshold = level;
	}

	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand(GetChannel(m_digitalChannelBase)->GetHwname() + ":THRESH " + to_string(level));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Checking for validity of configurations

bool DSLabsOscilloscope::CanEnableChannel(size_t /*i*/)
{
	return true;
}
