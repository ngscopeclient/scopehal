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

#ifdef _WIN32
#include <chrono>
#include <thread>
#endif

#include "scopehal.h"
#include "PicoVNA.h"
#include "EdgeTrigger.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Construction / destruction

PicoVNA::PicoVNA(SCPITransport* transport)
	: SCPIDevice(transport, true)
	, SCPIInstrument(transport)
	, m_triggerArmed(false)
	, m_triggerOneShot(true)
	, m_rbw(1)
{
	//Set up VNA in a known configuration (returns "OK", ignore reply but need to read it to stay synced)
	m_transport->SendCommandQueuedWithReply("*RST");

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

	//Get initial number of points
	auto spoints = Trim(m_transport->SendCommandQueuedWithReply("SENS:SWE:POIN?"));
	m_sampleDepth = stoi(spoints);

	//Immediate trigger
	m_transport->SendCommandQueuedWithReply("TRIG:SOUR IMM");
}

/**
	@brief Color the channels based on Pico's standard color sequence (blue-red-green-yellow-purple-gray-cyan-magenta)
 */
string PicoVNA::GetChannelColor(size_t i)
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

PicoVNA::~PicoVNA()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Device interface functions

string PicoVNA::GetDriverNameInternal()
{
	return "picovna";
}

OscilloscopeChannel* PicoVNA::GetExternalTrigger()
{
	//FIXME
	return NULL;
}

Oscilloscope::TriggerMode PicoVNA::PollTrigger()
{
	//Always report "triggered" so we can block on AcquireData() in ScopeThread
	//TODO: peek function of some sort?
	return TRIGGER_MODE_TRIGGERED;
}

bool PicoVNA::AcquireData()
{
	uint8_t format;
	uint16_t numActiveChannels;
	uint64_t sweepStartmHz;
	uint64_t sweepStopmHz;
	uint64_t numPoints;
	uint64_t updateFirstSample;
	uint64_t updateLastSample;

	double* mags[2][2];
	double* angles[2][2];
	size_t numPointsAllocated = 0;
	size_t expectedFirstSample = 0;
	bool first = true;

	double tstart = 0;
	int64_t fs = 0;

	//For now, we don't support streaming of partial waveforms.
	//Just wait for the complete waveform to show up, then display it in one block.
	while(true)
	{
		//Read the packet header
		//This is inefficient but the VNA is so slow the overhead is insignificant
		if(!m_transport->ReadRawData(sizeof(format), (uint8_t*)&format))
			return false;
		if(format != 0)
		{
			LogError("Expected data format %d, got %d\n", 0, format);
			exit(1);
		}
		if(!m_transport->ReadRawData(sizeof(numActiveChannels), (uint8_t*)&numActiveChannels))
			return false;
		if(!m_transport->ReadRawData(sizeof(sweepStartmHz), (uint8_t*)&sweepStartmHz))
			return false;
		if(!m_transport->ReadRawData(sizeof(sweepStopmHz), (uint8_t*)&sweepStopmHz))
			return false;
		if(!m_transport->ReadRawData(sizeof(numPoints), (uint8_t*)&numPoints))
			return false;
		if(!m_transport->ReadRawData(sizeof(updateFirstSample), (uint8_t*)&updateFirstSample))
			return false;
		if(!m_transport->ReadRawData(sizeof(updateLastSample), (uint8_t*)&updateLastSample))
			return false;

		//Sanity check
		if(numActiveChannels == 0)
		{
			LogError("PicoVNA::AcquireData: nothing to do, no active channels\n");
			return false;
		}
		if(numPoints == 0)
		{
			LogError("PicoVNA::AcquireData: nothing to do, no samples in sweep\n");
			return false;
		}

		//TODO: flag which channels are actually valid
		if(numActiveChannels != 4)
		{
			LogError("PicoVNA::AcquireData: partial acquisitions (not all four 2-port S-parameters) unimplemented\n");
			return false;
		}

		//If this is the first block in the sweep, allocate buffers
		if(first)
		{
			//Save capture timestamp (TODO: get this in header)
			tstart = GetTime();
			fs = (tstart - floor(tstart)) * FS_PER_SECOND;

			for(int i=0; i<2; i++)
			{
				for(int j=0; j<2; j++)
				{
					mags[i][j] = new double[numPoints];
					angles[i][j] = new double[numPoints];
				}
			}

			first = false;
			numPointsAllocated = numPoints;
		}

		//If a continuation, we expect the same number of points we started with
		else if(numPointsAllocated != numPoints)
		{
			LogError("PicoVNA::AcquireData: sample count changed\n");
			return false;
		}

		//We should start right after the last point, and sweep monotonically upward within the sample range
		if(updateFirstSample != expectedFirstSample)
		{
			LogError("PicoVNA::AcquireData: expected update to start at sample %zu\n", expectedFirstSample);
			return false;
		}
		if(updateLastSample < updateFirstSample)
		{
			LogError("PicoVNA::AcquireData: expected update to end after it started (invalid sample indexes)\n");
			return false;
		}
		if(updateLastSample >= numPointsAllocated)
		{
			LogError("PicoVNA::AcquireData: update contains samples beyond end of sweep\n");
			return false;
		}
		uint64_t numSamplesInBlock = updateLastSample - updateFirstSample + 1;

		//Read the sample blocks
		uint8_t txPort;
		uint8_t rxPort;
		for(uint16_t j=0; j<numActiveChannels; j++)
		{
			if(!m_transport->ReadRawData(sizeof(txPort), &txPort))
				return false;
			if(!m_transport->ReadRawData(sizeof(rxPort), &rxPort))
				return false;

			if( (txPort >= 2) || (rxPort >= 2) )
			{
				LogError("PicoVNA::AcquireData: update contains invalid port indexes\n");
				return false;
			}

			//Finally, time to read sample data!
			if(!m_transport->ReadRawData(
				sizeof(double)*numSamplesInBlock,
				(uint8_t*)&mags[rxPort][txPort][updateFirstSample]))
			{
				return false;
			}
			if(!m_transport->ReadRawData(
				sizeof(double)*numSamplesInBlock,
				(uint8_t*)&angles[rxPort][txPort][updateFirstSample]))
			{
				return false;
			}
		}

		expectedFirstSample = updateLastSample + 1;

		//Did we read the last sample?
		if(expectedFirstSample >= numPointsAllocated)
			break;
	}

	float sweepSpanmHz = sweepStopmHz - sweepStartmHz;
	m_rbw = (sweepSpanmHz * 1e-3) / numPointsAllocated;;

	//If we sent a stop command mid acquisition, discard the data now.
	//So only proceed here if trigger is armed.
	float angscale = 180 / M_PI;	//we use degrees for display
	bool skipping = false;
	if(!m_triggerArmed)
		skipping = true;
	else
	{
		//Process and convert sample data
		SequenceSet s;
		for(int dest=0; dest<2; dest++)
		{
			for(int src=0; src<2; src++)
			{
				int nchan = dest*2 + src;
				auto chan = m_channels[nchan];

				double* mag = mags[dest][src];
				double* angle = angles[dest][src];

				//Create the waveforms
				auto mcap = new UniformAnalogWaveform;
				mcap->m_timescale = m_rbw;
				mcap->m_triggerPhase = sweepStartmHz * 1e-3;
				mcap->m_startTimestamp = floor(tstart);
				mcap->m_startFemtoseconds = fs;
				mcap->PrepareForCpuAccess();

				auto acap = new UniformAnalogWaveform;
				acap->m_timescale = m_rbw;
				acap->m_triggerPhase = sweepStartmHz * 1e-3;
				acap->m_startTimestamp = floor(tstart);
				acap->m_startFemtoseconds = fs;
				acap->PrepareForCpuAccess();

				//Make content for display (dB and degrees)
				mcap->Resize(numPointsAllocated);
				acap->Resize(numPointsAllocated);
				for(size_t i=0; i<numPointsAllocated; i++)
				{
					mcap->m_samples[i] = 20 * log10(mag[i]);
					acap->m_samples[i] = angle[i] * angscale;
				}

				acap->MarkModifiedFromCpu();
				mcap->MarkModifiedFromCpu();

				s[StreamDescriptor(chan, 0)] = mcap;
				s[StreamDescriptor(chan, 1)] = acap;
			}
		}

		//Save the waveforms to our queue
		m_pendingWaveformsMutex.lock();
		m_pendingWaveforms.push_back(s);
		m_pendingWaveformsMutex.unlock();
	}

	//Done, clean up
	for(int i=0; i<2; i++)
	{
		for(int j=0; j<2; j++)
		{
			delete[] mags[i][j];
			delete[] angles[i][j];
		}
	}

	//If this was a one-shot trigger we're no longer armed
	if(m_triggerOneShot)
		m_triggerArmed = false;

	//If continuous trigger, re-arm for another acquisition
	else if(m_triggerArmed)
		m_transport->SendCommandQueued("INIT");

	return !skipping;
}

void PicoVNA::Start()
{
	m_transport->SendCommandQueued("INIT");
	m_triggerArmed = true;
	m_triggerOneShot = false;

	m_transport->FlushCommandQueue();
}

void PicoVNA::StartSingleTrigger()
{
	m_transport->SendCommandQueued("INIT");
	m_triggerArmed = true;
	m_triggerOneShot = true;

	m_transport->FlushCommandQueue();
}

void PicoVNA::Stop()
{
	m_transport->SendCommandQueued("ABOR");
	m_triggerArmed = false;
	m_triggerOneShot = false;

	m_transport->FlushCommandQueue();
}

void PicoVNA::ForceTrigger()
{
	m_transport->SendCommandQueued("INIT");
	m_triggerArmed = true;
	m_triggerOneShot = true;

	m_transport->FlushCommandQueue();
}

bool PicoVNA::IsTriggerArmed()
{
	return m_triggerArmed;
}

vector<uint64_t> PicoVNA::GetSampleDepthsNonInterleaved()
{
	vector<uint64_t> ret;
	ret.push_back(201);
	ret.push_back(501);
	ret.push_back(1001);
	ret.push_back(2001);
	ret.push_back(5001);
	ret.push_back(10001);
	return ret;
}

uint64_t PicoVNA::GetSampleDepth()
{
	return m_sampleDepth;
}

void PicoVNA::SetSampleDepth(uint64_t depth)
{
}

void PicoVNA::PullTrigger()
{
	//pulling not needed, we always have a valid trigger cached
}

void PicoVNA::PushTrigger()
{
	//do nothing
}

int64_t PicoVNA::GetResolutionBandwidth()
{
	return m_rbw;
}

void PicoVNA::SetSpan(int64_t span)
{
}

int64_t PicoVNA::GetSpan()
{
	return 0;
}

void PicoVNA::SetCenterFrequency(size_t channel, int64_t freq)
{
}

int64_t PicoVNA::GetCenterFrequency(size_t channel)
{
	return 0;
}
