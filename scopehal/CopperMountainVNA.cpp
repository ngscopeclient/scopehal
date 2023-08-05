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

#include "scopehal.h"
#include "CopperMountainVNA.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

CopperMountainVNA::CopperMountainVNA(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, m_rbw(1)
{
	//For now, assume we're a 2-port VNA only
	auto snport = m_transport->SendCommandQueuedWithReply("SERV:PORT:COUN?");
	int nports = stoi(snport);
	if(nports != 2)
		LogWarning("CopperMountainVNA driver only supports 2-port VNAs so far\n");

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

	//Apparently binary data transfer is not supported over TCP sockets since they ONLY use newline as end of message
	//while HiSLIP does not support pipelining of commands? That's derpy...

	//Set trigger source to internal
	m_transport->SendCommandQueued("TRIG:SOUR INT");

	//Turn off continuous trigger sweep
	m_transport->SendCommandQueued("INIT:CONT OFF");

	//Turn on RF power
	m_transport->SendCommandQueued("OUTP ON");

	//Set the channels we want to look at
	m_transport->SendCommandQueued("CALC:PAR1:DEF S11");
	m_transport->SendCommandQueued("CALC:PAR2:DEF S21");
	m_transport->SendCommandQueued("CALC:PAR3:DEF S12");
	m_transport->SendCommandQueued("CALC:PAR4:DEF S22");

	//Format polar (real + imag)
	m_transport->SendCommandQueued("CALC:TRAC1:FORM POL");
	m_transport->SendCommandQueued("CALC:TRAC2:FORM POL");
	m_transport->SendCommandQueued("CALC:TRAC3:FORM POL");
	m_transport->SendCommandQueued("CALC:TRAC4:FORM POL");

	//Get and cache resolution bandwidth
	auto srbw = m_transport->SendCommandQueuedWithReply("SENS:BWID?");
	Unit hz(Unit::UNIT_HZ);
	m_rbw = hz.ParseString(srbw);

	//Get and cache memory depth
	auto sdepth = m_transport->SendCommandQueuedWithReply("SENS:SWE:POIN?");
	m_memoryDepth = stoi(sdepth);

	//Get and cache start and stop frequency
	auto sfreq = m_transport->SendCommandQueuedWithReply("SENS:FREQ:STAR?");
	m_sweepStart = hz.ParseString(sfreq);
	sfreq = m_transport->SendCommandQueuedWithReply("SENS:FREQ:STOP?");
	m_sweepStop = hz.ParseString(sfreq);

	//Get and cache upper/lower freq limits of the instrument
	sfreq = m_transport->SendCommandQueuedWithReply("SERV:SWE:FREQ:MAX?");
	m_freqMax = hz.ParseString(sfreq);
	sfreq = m_transport->SendCommandQueuedWithReply("SERV:SWE:FREQ:MIN?");
	m_freqMin = hz.ParseString(sfreq);
}

CopperMountainVNA::~CopperMountainVNA()
{
	m_transport->SendCommandQueued("OUTP OFF");
}

/**
	@brief Color the channels (blue-red-green-yellow-purple-gray-cyan-magenta)
 */
string CopperMountainVNA::GetChannelColor(size_t i)
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device enumeration

string CopperMountainVNA::GetDriverNameInternal()
{
	return "coppermt";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Driver logic

//TODO: support ext trig if any
OscilloscopeChannel* CopperMountainVNA::GetExternalTrigger()
{
	return nullptr;
}

Oscilloscope::TriggerMode CopperMountainVNA::PollTrigger()
{
	auto state = Trim(m_transport->SendCommandQueuedWithReply("TRIG:STAT?"));

	//Pending, but no data yet
	if( (state ==  "MEAS") || (state == "WTRG") )
		return TRIGGER_MODE_RUN;

	else //if(state == "HOLD")
	{
		if(m_triggerArmed)
			return TRIGGER_MODE_TRIGGERED;
		else
			return TRIGGER_MODE_STOP;
	}
}

void CopperMountainVNA::Start()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommandQueued("INIT:IMM");
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void CopperMountainVNA::StartSingleTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommandQueued("INIT:IMM");
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void CopperMountainVNA::Stop()
{
	m_transport->SendCommandQueued("ABOR");
	m_triggerArmed = false;
	m_triggerOneShot = false;
}

void CopperMountainVNA::ForceTrigger()
{
	lock_guard<recursive_mutex> lock(m_mutex);
	m_transport->SendCommandQueued("INIT:IMM");
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

bool CopperMountainVNA::IsTriggerArmed()
{
	//return m_triggerArmed;
	return true;
}

void CopperMountainVNA::PushTrigger()
{
}

void CopperMountainVNA::PullTrigger()
{
}

bool CopperMountainVNA::AcquireData()
{
	m_transport->SendCommandQueuedWithReply("*OPC?");

	SequenceSet s;
	double tstart = GetTime();
	int64_t fs = (tstart - floor(tstart)) * FS_PER_SECOND;

	for(size_t dest = 0; dest<2; dest ++)
	{
		for(size_t src=0; src<2; src++)
		{
			//Hardware name of the channel
			string chname = "S" + to_string(dest+1) + to_string(src+1);

			int nparam = dest*2 + src;
			auto sdata = m_transport->SendCommandQueuedWithReply(
				string("CALC:TRAC") + to_string(nparam+1) + ":DATA:FDAT?");

			auto values = explode(sdata, ',');
			size_t npoints = values.size() / 2;

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
				float real = stof(values[i*2]);
				float imag = stof(values[i*2 + 1]);

				float mag = sqrt(real*real + imag*imag);
				float angle = atan2(imag, real);

				mcap->m_samples[i] = 20 * log10(mag);
				acap->m_samples[i] = angle * 180 / M_PI;
			}

			acap->MarkModifiedFromCpu();
			mcap->MarkModifiedFromCpu();

			auto chan = GetChannel(nparam);
			s[StreamDescriptor(chan, 0)] = mcap;
			s[StreamDescriptor(chan, 1)] = acap;
		}
	}

	//Save the waveforms to our queue
	m_pendingWaveformsMutex.lock();
	m_pendingWaveforms.push_back(s);
	m_pendingWaveformsMutex.unlock();

	//If this was a one-shot trigger we're no longer armed
	if(m_triggerOneShot)
		m_triggerArmed = false;

	//If continuous trigger, re-arm for another acquisition
	else if(m_triggerArmed)
	{
		//m_transport->SendCommand("*TRG");
		m_transport->SendCommandQueued("INIT:IMM");
	}

	return true;
}

vector<uint64_t> CopperMountainVNA::GetSampleDepthsNonInterleaved()
{
	vector<uint64_t> ret;
	ret.push_back(100);
	ret.push_back(200);
	ret.push_back(500);
	ret.push_back(1000);
	ret.push_back(2000);
	ret.push_back(5000);
	ret.push_back(10000);
	return ret;
}

uint64_t CopperMountainVNA::GetSampleDepth()
{
	return m_memoryDepth;
}

void CopperMountainVNA::SetSampleDepth(uint64_t depth)
{
	m_memoryDepth = depth;
	m_transport->SendCommandQueued(string("SENS:SWE:POIN ") + to_string(m_memoryDepth));
}

int64_t CopperMountainVNA::GetResolutionBandwidth()
{
	return m_rbw;
}

void CopperMountainVNA::SetSpan(int64_t span)
{
	//Calculate requested start/stop
	auto freq = GetCenterFrequency(0);
	m_sweepStart = freq - span/2;
	m_sweepStop = freq + span/2;

	//Clamp to instrument limits
	m_sweepStart = max(m_freqMin, m_sweepStart);
	m_sweepStop = min(m_freqMax, m_sweepStop);

	//Send to hardware
	m_transport->SendCommandQueued(string("SENS:FREQ:STAR ") + to_string(m_sweepStart));
	m_transport->SendCommandQueued(string("SENS:FREQ:STOP ") + to_string(m_sweepStop));
}

int64_t CopperMountainVNA::GetSpan()
{
	return m_sweepStop - m_sweepStart;
}

void CopperMountainVNA::SetCenterFrequency(size_t /*channel*/, int64_t freq)
{
	//Calculate requested start/stop
	auto span = GetSpan();
	m_sweepStart = freq - span/2;
	m_sweepStop = freq + span/2;

	//Clamp to instrument limits
	m_sweepStart = max(m_freqMin, m_sweepStart);
	m_sweepStop = min(m_freqMax, m_sweepStop);

	//Send to hardware
	m_transport->SendCommandQueued(string("SENS:FREQ:STAR ") + to_string(m_sweepStart));
	m_transport->SendCommandQueued(string("SENS:FREQ:STOP ") + to_string(m_sweepStop));
}

int64_t CopperMountainVNA::GetCenterFrequency(size_t /*channel*/)
{
	return (m_sweepStop + m_sweepStart) / 2;
}
