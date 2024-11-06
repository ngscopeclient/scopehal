/***********************************************************************************************************************
*                                                                                                                      *
* ngscopeclient                                                                                                        *
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
	@author Frederic Borry
	@brief Implementation of NanoVNA
	@ingroup vnadrivers
 */


#ifdef _WIN32
#include <chrono>
#include <thread>
#endif

#include "scopehal.h"
#include "NanoVNA.h"
#include "EdgeTrigger.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Construction / destruction

NanoVNA::NanoVNA(SCPITransport* transport)
	: SCPIDevice(transport, false)
	, SCPIInstrument(transport, false)
	, CommandLineDriver(transport)
	, m_triggerArmed(false)
	, m_triggerOneShot(false)
	, m_rbw(1000)
{
	
	m_maxResponseSize = 100*1024;
	 // 30s has a sweep with low rbw can take several minutes and we may have to wait that long between each data reception)
	m_communicationTimeout = 30;
	std::vector<string> info;
	string version =  ConverseSingle("version");
	if(version.empty())
	{
		LogError("Could not connect to NanoVNA :-/\n");
		return;
	}
	// Set vendor and version
	m_vendor= "NanoVNA";
	m_fwVersion = version;
	LogDebug("Version = %s\n",m_fwVersion.c_str());

	// Get model out of first line of info command response
	string infoLine = ConverseString("info");
	size_t pos = infoLine.find("NanoVNA");
 	if (pos!=std::string::npos)
	{
		string rest = infoLine.substr(pos);
		pos = rest.find_first_of(" \r\n\t");
		m_model = pos!=std::string::npos ? rest.substr(0,pos) : rest;
		LogDebug("Model = %s\n",m_model.c_str());
		if(m_model.find("-H 4")!= std::string::npos)
			m_nanoVNAModel = Model::MODEL_NANOVNA_H4;
		else if(m_model.find("-H")!= std::string::npos)
			m_nanoVNAModel = Model::MODEL_NANOVNA_H;
		else if(m_model.find("-F_V2")!= std::string::npos)
			m_nanoVNAModel = Model::MODEL_NANOVNA_F_V2;
		else if(m_model.find("-F")!= std::string::npos)
		{
			if(infoLine.find("deepelec")!=std::string::npos)
				m_nanoVNAModel = Model::MODEL_NANOVNA_F_DEEPELEC;
			else
				m_nanoVNAModel = Model::MODEL_NANOVNA_F;
		}
		else if(m_model.find("-D")!= std::string::npos)
			m_nanoVNAModel = Model::MODEL_NANOVNA_D;
		else
			m_nanoVNAModel = MODEL_NANOVNA;
		LogDebug("Model# = %d\n",m_nanoVNAModel);
	}
	else
	{
		LogWarning("Could not find model in info string '%s'.\n",infoLine.c_str());
		m_nanoVNAModel = MODEL_UNKNOWN;
	}

	// Setup device specific values
	switch (m_nanoVNAModel)
	{
		case MODEL_NANOVNA_F_V2:
			m_freqMin = 10000L;
			m_freqMax = 3000000000L;	// 3GHz
			m_maxDeviceSampleDepth = 301;
			break;
		case MODEL_NANOVNA_F:
		case MODEL_NANOVNA_H:
		case MODEL_NANOVNA_D:
		case MODEL_NANOVNA_F_DEEPELEC:
			m_freqMin = 10000L;
			m_freqMax = 1500000000L;	// 1.5GHz
			m_maxDeviceSampleDepth = 301;
			break;
		case MODEL_NANOVNA_H4:
			m_freqMin = 10000L;
			m_freqMax = 1500000000L;	// 1.5GHz
			m_maxDeviceSampleDepth = 401;
			break;
		case MODEL_NANOVNA:
		default:
			m_freqMin = 10000L;
			m_freqMax = 300000000L;		// 300 MHz
			m_maxDeviceSampleDepth = 101;
			break;
	}
	// Setup rbw values
	switch (m_nanoVNAModel)
	{
		case MODEL_NANOVNA_D:
			m_rbwValues[10]=363;
			m_rbwValues[33]=117;
			m_rbwValues[50]=78;
			m_rbwValues[100]=39;
			m_rbwValues[200]=19;
			m_rbwValues[250]=15;
			m_rbwValues[333]=11;
			m_rbwValues[500]=7;
			m_rbwValues[1000]=3;
			m_rbwValues[2000]=1;
			m_rbwValues[4000]=0;
			break;
		case MODEL_NANOVNA_F_DEEPELEC:
			m_rbwValues[10]=90;
			m_rbwValues[33]=29;
			m_rbwValues[50]=19;
			m_rbwValues[100]=9;
			m_rbwValues[200]=4;
			m_rbwValues[250]=3;
			m_rbwValues[333]=2;
			m_rbwValues[500]=1;
			m_rbwValues[1000]=0;
			break;
		case MODEL_NANOVNA_F_V2:
		case MODEL_NANOVNA_F:
		case MODEL_NANOVNA_H:
		case MODEL_NANOVNA_H4:
		case MODEL_NANOVNA:
		default:
			m_rbwValues[10]=10;
			m_rbwValues[30]=30;
			m_rbwValues[100]=100;
			m_rbwValues[300]=300;
			m_rbwValues[1000]=1000;
			break;
	}


	// Get span information, format is "<start> <stop> <points>"
	ConverseSweep(m_sweepStart,m_sweepStop,m_sampleDepth);

	//Add analog channel objects
	for(size_t dest = 1; dest<=2; dest ++)
	{
		// Only S21 and S11 available on NanoVNA
		//Hardware name of the channel
		string chname = "S" + to_string(dest) + "1";

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

/**
	@brief Color the channels based on Pico's standard color sequence (blue-red-green-yellow-purple-gray-cyan-magenta)
 */
string NanoVNA::GetChannelColor(size_t i)
{
	switch(i)
	{
		case 0:
			return "#ffff00";
		case 1:
		default:
			return "#00ffff";
	}
}

NanoVNA::~NanoVNA()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Device interface functions

string NanoVNA::GetDriverNameInternal()
{
	return "nanovna";
}

// Trigger management

OscilloscopeChannel* NanoVNA::GetExternalTrigger()
{
	return NULL;
}

Oscilloscope::TriggerMode NanoVNA::PollTrigger()
{
	return m_triggerArmed ? TRIGGER_MODE_TRIGGERED : TRIGGER_MODE_STOP;
}
void NanoVNA::Start()
{
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void NanoVNA::StartSingleTrigger()
{
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void NanoVNA::Stop()
{
	m_triggerArmed = false;
	m_triggerOneShot = false;
}

void NanoVNA::ForceTrigger()
{
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

bool NanoVNA::IsTriggerArmed()
{
	return m_triggerArmed;
}

void NanoVNA::PullTrigger()
{
	//pulling not needed, we always have a valid trigger cached
}

void NanoVNA::PushTrigger()
{
	//do nothing
}

// Sample depth management

vector<uint64_t> NanoVNA::GetSampleDepthsNonInterleaved()
{
	vector<uint64_t> ret;
	ret.push_back(11);
	ret.push_back(51);
	ret.push_back(101);
	ret.push_back(201);
	ret.push_back(301);
	ret.push_back(501);
	ret.push_back(1001);
	ret.push_back(2001);
	ret.push_back(5001);
	ret.push_back(10001);
	return ret;
}

uint64_t NanoVNA::GetSampleDepth()
{
	return m_sampleDepth;
}

void NanoVNA::SetSampleDepth(uint64_t depth)
{
	m_sampleDepth = depth;
}

bool NanoVNA::AcquireData()
{

	// LogDebug("Acquiring data\n");

	// Notify about download operation start
	ChannelsDownloadStarted();

	// Store sample depth value
	size_t npoints = m_sampleDepth;
	string command = "scan " + std::to_string(m_sweepStart) + " " + std::to_string(m_sweepStop) + " " + std::to_string(npoints) + " 0b110";
	std::vector<string> values;
	auto progress = [this/*, page, pages*/] (float fprogress) {
		float linear_progress = fprogress;// ((float)page + fprogress) / (float)pages;
		// We're downloading both channels at the same time
		ChannelsDownloadStatusUpdate(0, InstrumentChannel::DownloadState::DOWNLOAD_IN_PROGRESS, linear_progress);
		ChannelsDownloadStatusUpdate(1, InstrumentChannel::DownloadState::DOWNLOAD_IN_PROGRESS, linear_progress);
	};
	size_t read = ConverseMultiple(command,values,true,progress,npoints+1);
	if(read != (npoints+1))
	{
		LogError("Invalid number of acquired bytes: %zu, expected %zu. Ingoring capture.\n",read,npoints);
		return false;
	}

	std::vector<std::vector<float>> data;
	// Parse data
	for(size_t i=0; i<npoints; i++)
	{
		// Split sample line into strings
		auto value = explode(values[i],' ');
		if(value.size()!=4)
		{
			LogError("Could not find 4 values in data line '%s', aborting capture.\n",values[i].c_str());
			return false;
		}
		std::vector<float> points;
		for(size_t j = 0; j < 4 ; j++)
		{
			points.push_back(stof(value[j]));
		}
		data.push_back(points);
	}

	SequenceSet s;
	double tstart = GetTime();
	int64_t fs = (tstart - floor(tstart)) * FS_PER_SECOND;

	for(size_t dest = 0; dest<2; dest ++)
	{
		//Hardware name of the channel
		string chname = "S" + to_string(dest+1) + "1";

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
			float real = data[i][dest];
			float imag = data[i][dest+1];

			float mag = sqrt(real*real + imag*imag);
			float angle = atan2(imag, real);

			mcap->m_samples[i] = 20 * log10(mag);
			acap->m_samples[i] = angle * 180 / M_PI;
		}

		acap->MarkModifiedFromCpu();
		mcap->MarkModifiedFromCpu();

		auto chan = GetChannel(dest);
		s[StreamDescriptor(chan, 0)] = mcap;
		s[StreamDescriptor(chan, 1)] = acap;
	}

	//Save the waveforms to our queue
	m_pendingWaveformsMutex.lock();
	m_pendingWaveforms.push_back(s);
	m_pendingWaveformsMutex.unlock();

	//If this was a one-shot trigger we're no longer armed
	if(m_triggerOneShot)
		m_triggerArmed = false;

	//LogDebug("Acquisition done\n");
	// Tell the download monitor that waveform download has finished
	ChannelsDownloadFinished();
	return true;
}

/**
 * @brief Set the bandwidth value
 *
 * @param bandwidth the value to set
 */
void NanoVNA::SendBandwidthValue(int64_t bandwidth)
{
	// Get currently configured rbw
	string response = ConverseSingle("bandwidth "+std::to_string(bandwidth));
	LogDebug("Bandwidth response = %s.\n",response.c_str());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Spectrum analyzer mode

int64_t NanoVNA::GetResolutionBandwidth()
{
	return m_rbw;
}

void NanoVNA::SetResolutionBandwidth(int64_t rbw)
{
	int64_t valueToSend = 0;
	for(auto it = m_rbwValues.begin(); it != m_rbwValues.end(); ++it)
	{
		if(rbw<=it->first || it == std::prev(m_rbwValues.end()))
		{
			m_rbw = it->first;
			valueToSend = it->second;
			break;
		}
	}
	SendBandwidthValue(valueToSend);
}

void NanoVNA::SetSpan(int64_t span)
{
	//Calculate requested start/stop
	auto freq = GetCenterFrequency(0);
	m_sweepStart = freq - span/2;
	m_sweepStop = freq + span/2;

	//Clamp to instrument limits
	m_sweepStart = max(m_freqMin, m_sweepStart);
	m_sweepStop = min(m_freqMax, m_sweepStop);

	// Send and read back the values to/from the devices to check boundaries
	ConverseSweep(m_sweepStart,m_sweepStop,true);
}

int64_t NanoVNA::GetSpan()
{
	return m_sweepStop - m_sweepStart;
}

void NanoVNA::SetCenterFrequency([[maybe_unused]] size_t channel, int64_t freq)
{
	//Calculate requested start/stop
	int64_t span = GetSpan();
	m_sweepStart = freq - span/2;
	m_sweepStop = freq + span/2;

	//Clamp to instrument limits
	m_sweepStart = max(m_freqMin, m_sweepStart);
	m_sweepStop = min(m_freqMax, m_sweepStop);

	// Send and read back the values to/from the devices to check boundaries
	ConverseSweep(m_sweepStart,m_sweepStop,true);
}

int64_t NanoVNA::GetCenterFrequency([[maybe_unused]] size_t channel)
{
	return (m_sweepStop + m_sweepStart) / 2;
}
