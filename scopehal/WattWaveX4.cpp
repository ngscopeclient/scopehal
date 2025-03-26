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
	@brief Implementation of WattWaveX4
	@ingroup scopedrivers
 */

#ifdef _WIN32
#include <chrono>
#include <thread>
#endif

#include "scopehal.h"
#include "WattWaveX4.h"
#include "EdgeTrigger.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Construction / destruction

/**
	@brief Initialize the driver

	@param transport	SCPITwinLanTransport pointing to a scopehal-waveforms-bridge instance
 */
WattWaveX4::WattWaveX4(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, RemoteBridgeOscilloscope(transport)
{
	//Set up initial cache configuration as "not valid" and let it populate as we go
	m_transport->SendCommand("ACQUIRE:DATA_OUT 0"); //may not be early enough -- or clear / reset needs implemented
	IdentifyHardware();

	//Add analog channel objects
	for(size_t i = 0; i < m_analogChannelCount; i++)
	{
		//Hardware name of the channel
		//string chname = "";
		string chname = string("Channel: ") + to_string(i+1);

		//Create the channel
		auto chan = new PowerMeterChannel(//OscilloscopeChannel(
			this,
			chname,
			GetChannelColor(i),
			//Unit(Unit::UNIT_FS),
			//Unit(ch_unit),
			//Stream::STREAM_TYPE_ANALOG,
			i);
		m_channels.push_back(chan);
		chan->SetDefaultDisplayName();

		//Set initial configuration so we have a well-defined instrument state
		m_channelAttenuations[i] = 1;
		//SetChannelCoupling(i, OscilloscopeChannel::COUPLE_DC_1M);
		SetChannelOffset(i, 0,  0);
		SetChannelVoltageRange(i, 0, 5);
		SetChannelOffset(i, 1,  0);  //stream 1
		SetChannelVoltageRange(i, 1, 1); //stream 1
	}


	//Set initial memory configuration to smallest depth / fastest rate supported
	auto rates = GetSampleRatesNonInterleaved();
  	SetSampleRate(rates[0]);

	auto depths = GetSampleDepthsNonInterleaved();
	SetSampleDepth(depths[0]);

	
	//Add the external trigger input
	/*m_extTrigChannel =
		new OscilloscopeChannel(this, "EX", Stream::STREAM_TYPE_TRIGGER, "", m_channels.size(), true);
	m_channels.push_back(m_extTrigChannel);
	m_extTrigChannel->SetDefaultDisplayName();*/
	

	//Configure the trigger
	auto trig = new EdgeTrigger(this);
	trig->SetType(EdgeTrigger::EDGE_RISING);
	trig->SetLevel(0);
	trig->SetInput(0, StreamDescriptor(GetOscilloscopeChannel(0)));
	SetTrigger(trig);
	PushTrigger();
	SetTriggerOffset(17);
}

/**
	@brief Color the channels based on Digilent's standard color sequence (yellow-cyan-magenta-green)
 */
string WattWaveX4::GetChannelColor(size_t i)
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
	@brief Parse model name text to figure out what the scope is
 */
void WattWaveX4::IdentifyHardware()
{
	if(m_model.find("X4") == 0)
		m_series = SERIES_WattWaveX4;
	else
		m_series = SERIES_UNKNOWN;

	//MSO channel support is still pending
	m_digitalChannelCount = 0;

	//Ask the scope how many channels it has
	m_transport->SendCommand("CHANS?");
	m_analogChannelCount = stoi(m_transport->ReadReply());
}

WattWaveX4::~WattWaveX4()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Accessors

unsigned int WattWaveX4::GetInstrumentTypes() const
{
	return Instrument::INST_OSCILLOSCOPE;
}

uint32_t WattWaveX4::GetInstrumentTypesForChannel([[maybe_unused]] size_t i) const
{
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Device interface functions

///@brief Return the constant driver name "digilent"
string WattWaveX4::GetDriverNameInternal()
{
	return "wattwave";
}

void WattWaveX4::FlushConfigCache()
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
}

vector<OscilloscopeChannel::CouplingType> WattWaveX4::GetAvailableCouplings([[maybe_unused]] size_t i)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	ret.push_back(OscilloscopeChannel::COUPLE_DC_1M);

	return ret;
}

double WattWaveX4::GetChannelAttenuation(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelAttenuations[i];
}

void WattWaveX4::SetChannelAttenuation(size_t i, double atten)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		m_channelAttenuations[i] = atten;
	}

	//send attenuation info to hardware
	lock_guard<recursive_mutex> lock(m_mutex);
	char buf[128];
	snprintf(buf, sizeof(buf), ":%s:ATTEN %f", GetOscilloscopeChannel(i)->GetHwname().c_str(), atten);
	m_transport->SendCommand(buf);
}

unsigned int WattWaveX4::GetChannelBandwidthLimit([[maybe_unused]] size_t i)
{
	return 0;
}

void WattWaveX4::SetChannelBandwidthLimit([[maybe_unused]] size_t i, [[maybe_unused]] unsigned int limit_mhz)
{
}

OscilloscopeChannel* WattWaveX4::GetExternalTrigger()
{
	//FIXME
	return NULL;
}

Oscilloscope::TriggerMode WattWaveX4::PollTrigger()
{
	//Always report "triggered" so we can block on AcquireData() in ScopeThread
	//TODO: peek function of some sort?
	return TRIGGER_MODE_TRIGGERED;
}


#include <iostream>

bool WattWaveX4::AcquireData()
{
	this->ChannelsDownloadStatusUpdate(0, InstrumentChannel::DownloadState::DOWNLOAD_IN_PROGRESS, 0.0);


	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->FlushRXBuffer(); // flush buffer
	m_transport->SendCommand("ACQUIRE:DATA_OUT 1"); // start datastream out of device
	
    std::vector<meas_data_set> datasets;
	auto BUFFER_SIZE = (GetSampleDepth()+2) * sizeof(meas_data_set);
	std::vector<uint8_t> buffer(BUFFER_SIZE);
    /*static*/ size_t bufferLen = 0;         // Tracks valid bytes in buffer
	uint16_t counter_old=0;
	uint16_t reads = 0;

	
    while (true) {  // until mem depth is fullfiled
        // Read into the buffer
        ssize_t bytesRead = m_transport->ReadRawData(BUFFER_SIZE, &buffer[0] /*buffer.data()*/);
		reads++;
        if (bytesRead <= 0) {
			LogWarning("WattWave X4 Error: Serial read failed or timed out!\n");
            //break;  counter to give error
			}
			
		this->ChannelsDownloadStatusUpdate(0, InstrumentChannel::DownloadState::DOWNLOAD_IN_PROGRESS, 0.5);
		
        bufferLen /*+*/= bytesRead;
        // Process buffer while it contains full dataset(s)
        size_t i = 0;
        while (i + DATASET_SIZE <= bufferLen) {
            // Find the first STX occurrence in the remaining buffer
            while (i < bufferLen && buffer[i] != STX) {
                i++;  // Skip until STX is found
            }
            // Check if enough bytes remain for a full dataset
            if (i + DATASET_SIZE > bufferLen) {
                break; // Wait for more data in next read
            }
			// Ensure alignment before performing reinterpret_cast
			if (reinterpret_cast<std::uintptr_t>(&buffer[i]) % alignof(meas_data_set) != 0) {
				LogWarning("WattWave X4 Misaligned access detected! Falling back to memcpy.");
				meas_data_set dataset;
				std::memcpy(&dataset, &buffer[i], sizeof(meas_data_set));
				datasets.push_back(dataset);
			} else {
				// Safe to reinterpret_cast
				datasets.emplace_back(*reinterpret_cast<meas_data_set*>(&buffer[i]));
				}
            
			auto count = datasets.size();
			if (count == 1){
				counter_old = datasets[count-1].counter-1;
			}
			
			if (counter_old+1 != datasets[count-1].counter){
				LogWarning("WattWave X4 Missing data : %d Reads: %d \r\n",datasets[count-1].counter-counter_old,reads);
			}
			counter_old = datasets[count-1].counter;
			
			// Move buffer index to next possible dataset
			i += DATASET_SIZE;
			
			if (datasets.size() >= GetSampleDepth()){
			break;}
		}

        // Shift remaining unprocessed bytes to the start of the buffer
        if (i < bufferLen) {
            //std::memmove(buffer, &buffer[i], bufferLen - i);
			std::move(buffer.begin() + i, buffer.end(), buffer.begin());
        }
        bufferLen -= i;
		if (datasets.size() >= GetSampleDepth()){
			break;
			}
    }
	m_transport->SendCommand("ACQUIRE:DATA_OUT 0"); // stop datastream out of device
	m_transport->FlushRXBuffer(); // flush buffer
	
	
	SequenceSet s;

	for(int i=0; i<static_cast<int>(m_analogChannelCount); i++)
	{
		if(!m_channelsEnabled[i])
			continue;
			
			
	double t = GetTime();  // need to be alingned??
	int64_t fs = (t - floor(t)) * FS_PER_SECOND;
		//Create our waveform
		
		
	auto cap = new UniformAnalogWaveform;
	cap->m_timescale = 100e9;// 100us fs_per_sample;
	cap->m_triggerPhase = 1;//trigphase;
	cap->m_startTimestamp = time(NULL);
	cap->m_startFemtoseconds = 1;//fs;
	cap->Resize(datasets.size());
	cap->PrepareForCpuAccess();
	for(size_t j=0; j<datasets.size(); j++)
		{
		cap->m_samples[j] = datasets[j].meas_current[i];
		}
		
	auto cap_v = new UniformAnalogWaveform;
	cap_v->m_timescale = 100e9;// 100us fs_per_sample;
	cap_v->m_triggerPhase = 1;//trigphase;
	cap_v->m_startTimestamp = time(NULL);
	cap_v->m_startFemtoseconds = 1;//fs;
	cap_v->Resize(datasets.size());
	cap_v->PrepareForCpuAccess();
	for(size_t j=0; j<datasets.size(); j++)
		{
		cap_v->m_samples[j] = datasets[j].meas_voltage[i];
		}		

	cap_v->MarkSamplesModifiedFromCpu();	
	cap->MarkSamplesModifiedFromCpu();
	
	auto chan = GetChannel(i);
	s[StreamDescriptor(chan, 0)] = cap_v;
	s[StreamDescriptor(chan, 1)] = cap;
	}
	
		//Save the waveforms to our queue
	m_pendingWaveformsMutex.lock();
	m_pendingWaveforms.push_back(s);
	m_pendingWaveformsMutex.unlock();



	//If this was a one-shot trigger we're no longer armed
	if(m_triggerOneShot)
		m_triggerArmed = false;
		
		
	//If this was a one-shot trigger we're no longer armed
	if(m_triggerOneShot){
		m_triggerArmed = false;
	}
	this->ChannelsDownloadStatusUpdate(0, InstrumentChannel::DownloadState::DOWNLOAD_FINISHED, 1.0);
		
	return true;// datasets;
	
	
	







	//Acquire data for each channel
/*	size_t chnum;
	size_t memdepth;
	float trigphase;
	SequenceSet s;
	double t = GetTime();
	int64_t fs = (t - floor(t)) * FS_PER_SECOND;

	if(numChannels == 0)
		return false;

	//Analog channels get processed separately
	vector<double*> abufs;
	vector<UniformAnalogWaveform*> awfms;
	for(size_t i=0; i<numChannels; i++)
	{
		//Get channel ID and memory depth (samples, not bytes)
		if(!m_transport->ReadRawData(sizeof(chnum), (uint8_t*)&chnum))
			return false;
		if(!m_transport->ReadRawData(sizeof(memdepth), (uint8_t*)&memdepth))
			return false;
		double* buf = new double[memdepth];

		//Analog channels
		if(chnum < m_analogChannelCount)
		{
			abufs.push_back(buf);

			if(!m_transport->ReadRawData(sizeof(trigphase), (uint8_t*)&trigphase))
				return false;

			//TODO: stream timestamp from the server

			if(!m_transport->ReadRawData(memdepth * sizeof(double), (uint8_t*)buf))
				return false;

			//Create our waveform
			auto cap = new UniformAnalogWaveform;
			cap->m_timescale = fs_per_sample;
			cap->m_triggerPhase = trigphase;
			cap->m_startTimestamp = time(NULL);
			cap->m_startFemtoseconds = fs;
			cap->Resize(memdepth);
			awfms.push_back(cap);

			s[GetOscilloscopeChannel(chnum)] = cap;
		}
	}

	//Process analog captures in parallel
	#pragma omp parallel for
	for(size_t i=0; i<awfms.size(); i++)
	{
		auto cap = awfms[i];

		double* buf = abufs[i];
		cap->PrepareForCpuAccess();
		for(size_t j=0; j<memdepth; j++)
			cap->m_samples[j] = buf[j];
		cap->MarkSamplesModifiedFromCpu();

		delete[] abufs[i];
	}

	//Save the waveforms to our queue
	m_pendingWaveformsMutex.lock();
	m_pendingWaveforms.push_back(s);
	m_pendingWaveformsMutex.unlock();

	//If this was a one-shot trigger we're no longer armed
	if(m_triggerOneShot)
		m_triggerArmed = false;

	return true;*/
}

vector<uint64_t> WattWaveX4::GetSampleRatesNonInterleaved()
{
	vector<uint64_t> ret;

	string rates;
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand("ACQUIRE:RATES_SUPPORT?");
		rates = m_transport->ReadReply();
		
	}

	size_t i=0;
	while(true)
	{
		size_t istart = i;
		i = rates.find_first_of(",\r", i + 1);
		if(i == string::npos)
			break;

		auto block = rates.substr(istart, i-istart);
		auto fs = stoll(block);
		//auto hz = FS_PER_SECOND / fs;
		ret.push_back(fs);

		//skip the comma
		i++;
	}

	return ret;
}

vector<uint64_t> WattWaveX4::GetSampleRatesInterleaved()
{
	//interleaving not supported
	vector<uint64_t> ret = {};
	return ret;
}

set<Oscilloscope::InterleaveConflict> WattWaveX4::GetInterleaveConflicts()
{
	//interleaving not supported
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}




void WattWaveX4::SetSampleDepth(uint64_t depth)
{
	
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand("ACQUIRE:POINTS " + to_string(depth));
	m_mdepth = depth;
	//m_sampleDepth = depth;
	//m_sampleDepthValid = true;
}

void WattWaveX4::SetSampleRate(uint64_t rate)
{
	m_srate = rate;
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommand( string("ACQUIRE:RATES ") + to_string(rate));
}

vector<uint64_t> WattWaveX4::GetSampleDepthsNonInterleaved()
{
	vector<uint64_t> ret;

	string depths;
	{
		lock_guard<recursive_mutex> lock(m_mutex);
		m_transport->SendCommand("ACQUIRE:POINTS_SUPPORT?");
		depths = m_transport->ReadReply();
	}

	size_t i=0;
	while(true)
	{
		size_t istart = i;
		i = depths.find_first_of(",\r", i + 1); //depths.find(',', i+1);
		if(i == string::npos)
			break;

		ret.push_back(stol(depths.substr(istart, i-istart)));

		//skip the comma
		i++;
	}

	return ret;
}

vector<uint64_t> WattWaveX4::GetSampleDepthsInterleaved()
{
	//interleaving not supported
	vector<uint64_t> ret;
	return ret;
}

bool WattWaveX4::IsInterleaving()
{
	//not supported
	return false;
}

bool WattWaveX4::SetInterleaving([[maybe_unused]] bool combine)
{
	//not supported
	return false;
}

void WattWaveX4::PushTrigger()
{
	auto et = dynamic_cast<EdgeTrigger*>(m_trigger);
	if(et)
		PushEdgeTrigger(et);

	else
		LogWarning("Unknown trigger type (not an edge)\n");

	ClearPendingWaveforms();
}

vector<Oscilloscope::AnalogBank> WattWaveX4::GetAnalogBanks()
{
	vector<AnalogBank> banks;
	banks.push_back(GetAnalogBank(0));
	return banks;
}

Oscilloscope::AnalogBank WattWaveX4::GetAnalogBank([[maybe_unused]] size_t channel)
{
	AnalogBank bank;
	return bank;
}

bool WattWaveX4::IsADCModeConfigurable()
{
	return false;
}

vector<string> WattWaveX4::GetADCModeNames([[maybe_unused]] size_t channel)
{
	vector<string> ret;
	return ret;
}

size_t WattWaveX4::GetADCMode([[maybe_unused]] size_t channel)
{
	return 0;
}

void WattWaveX4::SetADCMode([[maybe_unused]] size_t channel, [[maybe_unused]] size_t mode)
{
	//not supported
}

bool WattWaveX4::CanEnableChannel([[maybe_unused]] size_t channel)
{
	//all channels always available, no resource sharing
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Logic analyzer configuration
/*
vector<Oscilloscope::DigitalBank> WattWaveX4::GetDigitalBanks()
{
	vector<DigitalBank> banks;
	return banks;
}

Oscilloscope::DigitalBank WattWaveX4::GetDigitalBank([[maybe_unused]] size_t channel)
{
	DigitalBank ret;
	return ret;
}

bool WattWaveX4::IsDigitalHysteresisConfigurable()
{
	return false;
}

bool WattWaveX4::IsDigitalThresholdConfigurable()
{
	return false;
}

float WattWaveX4::GetDigitalHysteresis([[maybe_unused]] size_t channel)
{
	return 0;
}

float WattWaveX4::GetDigitalThreshold([[maybe_unused]] size_t channel)
{
	return 0;
}

void WattWaveX4::SetDigitalHysteresis([[maybe_unused]] size_t channel, [[maybe_unused]] float level)
{

}

void WattWaveX4::SetDigitalThreshold([[maybe_unused]] size_t channel, [[maybe_unused]] float level)
{

}*/

bool WattWaveX4::CanAverage(size_t /*i*/)
{
	return true;
}

size_t WattWaveX4::GetNumAverages(size_t /*i*/)
{
	return 1;
}

void WattWaveX4::SetNumAverages(size_t /*i*/, size_t /*navg*/)
{

}

bool WattWaveX4::CanInterleave()
{
	return false;
}


float WattWaveX4::GetChannelVoltageRange(size_t i, size_t stream)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return local_channelVoltageRanges[i][stream];
}

void WattWaveX4::SetChannelVoltageRange(size_t i, size_t stream, float range)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		local_channelVoltageRanges[i][stream] = range;
		LogWarning("CH: %zu - stream: %zu\n",i,stream);
	}

	lock_guard<recursive_mutex> lock(m_mutex);
	char buf[128];
	snprintf(buf, sizeof(buf), ":%s:RANGE %f", m_channels[i]->GetHwname().c_str(), range / GetChannelAttenuation(i));
	m_transport->SendCommand(buf);
}

float WattWaveX4::GetChannelOffset(size_t i, size_t stream)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return local_channelOffsets[i][stream];
}

void WattWaveX4::SetChannelOffset(size_t i, size_t stream, float offset)
{
	{
		lock_guard<recursive_mutex> lock(m_cacheMutex);
		local_channelOffsets[i][stream] = offset;
		LogDebug("ch:%zu - stream%zu \n", i,stream);
	}

	lock_guard<recursive_mutex> lock(m_mutex);
	char buf[128];
	snprintf(buf, sizeof(buf), ":%s:OFFS %f", m_channels[i]->GetHwname().c_str(), -offset / GetChannelAttenuation(i));
	m_transport->SendCommand(buf);
}

