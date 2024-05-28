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

#ifdef _WIN32
#include <chrono>
#include <thread>
#endif

#include "scopehal.h"
#include "ThunderScopeOscilloscope.h"
#include "EdgeTrigger.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Construction / destruction

ThunderScopeOscilloscope::ThunderScopeOscilloscope(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, RemoteBridgeOscilloscope(transport, true)
	, m_diag_hardwareWFMHz(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ))
	, m_diag_receivedWFMHz(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ))
	, m_diag_totalWFMs(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS))
	, m_diag_droppedWFMs(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS))
	, m_diag_droppedPercent(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_PERCENT))
{
	m_analogChannelCount = 4;

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

		string nicename = "ch" + to_string(i+1);
		chan->SetDisplayName(nicename);

		//Set initial configuration so we have a well-defined instrument state
		m_channelAttenuations[i] = 10;
		SetChannelCoupling(i, OscilloscopeChannel::COUPLE_DC_1M);
		SetChannelOffset(i, 0,  0);
		SetChannelVoltageRange(i, 0, 5);
	}

	//Set initial memory configuration.
	SetSampleRate(1000000L);
	SetSampleDepth(10000);

	//Set up the data plane socket
	auto csock = dynamic_cast<SCPITwinLanTransport*>(m_transport);
	if(!csock)
		LogFatal("ThunderScopeOscilloscope expects a SCPITwinLanTransport\n");

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

void ThunderScopeOscilloscope::ResetPerCaptureDiagnostics()
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
string ThunderScopeOscilloscope::GetChannelColor(size_t i)
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
		default:
			return "#ffff00";
	}
}

ThunderScopeOscilloscope::~ThunderScopeOscilloscope()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Accessors

unsigned int ThunderScopeOscilloscope::GetInstrumentTypes() const
{
	return Instrument::INST_OSCILLOSCOPE;
}

uint32_t ThunderScopeOscilloscope::GetInstrumentTypesForChannel(size_t /*i*/) const
{
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Device interface functions

string ThunderScopeOscilloscope::GetDriverNameInternal()
{
	return "thunderscope";
}

void ThunderScopeOscilloscope::FlushConfigCache()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
}

double ThunderScopeOscilloscope::GetChannelAttenuation(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelAttenuations[i];
}

void ThunderScopeOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	double oldAtten = m_channelAttenuations[i];
	m_channelAttenuations[i] = atten;

	//Rescale channel voltage range and offset
	double delta = atten / oldAtten;
	m_channelVoltageRanges[i] *= delta;
	m_channelOffsets[i] *= delta;
}

unsigned int ThunderScopeOscilloscope::GetChannelBandwidthLimit(size_t /*i*/)
{
	return 0;
}

void ThunderScopeOscilloscope::SetChannelBandwidthLimit(size_t /*i*/, unsigned int /*limit_mhz*/)
{
}

OscilloscopeChannel* ThunderScopeOscilloscope::GetExternalTrigger()
{
	//FIXME
	return NULL;
}

Oscilloscope::TriggerMode ThunderScopeOscilloscope::PollTrigger()
{
	//Always report "triggered" so we can block on AcquireData() in ScopeThread
	//TODO: peek function of some sort?
	return TRIGGER_MODE_TRIGGERED;
}

bool ThunderScopeOscilloscope::AcquireData()
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
	uint64_t fs_per_sample;
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

	//Acquire data for each channel
	uint8_t chnum;
	uint64_t memdepth;
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

			for (uint64_t ii = 0; ii < memdepth; ii++)
			{
				int8_t* p = (int8_t*)&buf[ii];
				if ((*p == -128) || (*p == 127))
				{
					clipping = 1;
				}
			}

			//Create our waveform
			UniformAnalogWaveform* cap = AllocateAnalogWaveform(m_nickname + "." + GetChannel(i)->GetHwname());
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
		} else {
			LogFatal("???\n");
		}
	}

	//Process analog captures in parallel
	#pragma omp parallel for
	for(size_t i=0; i<awfms.size(); i++)
	{
		auto cap = awfms[i];
		cap->PrepareForCpuAccess();
		Convert8BitSamples(
			(float*)&cap->m_samples[0],
			(int8_t*)abufs[i],
			scales[i],
			offsets[i],
			cap->m_samples.size());
		delete[] abufs[i];
		cap->MarkModifiedFromCpu();
	}

	//DEBUG: implement clientside trigger interpolation
	auto trig = GetTrigger();
	if(trig)
	{
		size_t trigSampleIndex = GetTriggerOffset() / fs_per_sample;
		auto chan = trig->GetInput(0).m_channel;
		auto data = dynamic_cast<UniformAnalogWaveform*>(s[chan]);

		//bounds check trigger against waveform size
		if( (trigSampleIndex < 1) || (trigSampleIndex >= data->size()) )
		{
			//no interpolation possible, trigger sample isn't in the buffer
		}

		else
		{
			float fa = data->m_samples[trigSampleIndex - 1];
			float fb = data->m_samples[trigSampleIndex];
			float slope = (fb - fa);
			float delta = trig->GetLevel() - fa;
			float trigphase = delta / slope;

			for(auto w : awfms)
				w->m_triggerPhase = fs_per_sample * (1 - trigphase);
		}
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

void ThunderScopeOscilloscope::Start()
{
	m_triggerArmed = true; //FIXME

	RemoteBridgeOscilloscope::Start();
	ResetPerCaptureDiagnostics();
}

void ThunderScopeOscilloscope::StartSingleTrigger()
{
	RemoteBridgeOscilloscope::StartSingleTrigger();
	ResetPerCaptureDiagnostics();
}

void ThunderScopeOscilloscope::ForceTrigger()
{
	RemoteBridgeOscilloscope::ForceTrigger();
	ResetPerCaptureDiagnostics();
}

vector<uint64_t> ThunderScopeOscilloscope::GetSampleRatesNonInterleaved()
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

vector<uint64_t> ThunderScopeOscilloscope::GetSampleRatesInterleaved()
{
	//interleaving not supported
	vector<uint64_t> ret = {};
	return ret;
}

set<Oscilloscope::InterleaveConflict> ThunderScopeOscilloscope::GetInterleaveConflicts()
{
	//interleaving not supported
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> ThunderScopeOscilloscope::GetSampleDepthsNonInterleaved()
{
	vector<uint64_t> ret;

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

vector<uint64_t> ThunderScopeOscilloscope::GetSampleDepthsInterleaved()
{
	//interleaving not supported
	vector<uint64_t> ret;
	return ret;
}

bool ThunderScopeOscilloscope::IsInterleaving()
{
	//interleaving not supported
	return false;
}

bool ThunderScopeOscilloscope::SetInterleaving(bool /*combine*/)
{
	//interleaving not supported
	return false;
}

vector<OscilloscopeChannel::CouplingType> ThunderScopeOscilloscope::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	ret.push_back(OscilloscopeChannel::COUPLE_DC_1M);
	ret.push_back(OscilloscopeChannel::COUPLE_AC_1M);
	return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Checking for validity of configurations

bool ThunderScopeOscilloscope::CanEnableChannel(size_t /*i*/)
{
	return true;
}
