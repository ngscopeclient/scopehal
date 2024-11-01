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
	@author Frederic Borry
	@brief Implementation of TinySA

	@ingroup scopedrivers
 */

#include "scopehal.h"
#include "TinySA.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initialize the driver

	@param transport	SCPITransport connected to the SA
 */
TinySA::TinySA(SCPITransport* transport)
	: SCPIDevice(transport,false)
	, SCPIInstrument(transport,false)
	, m_triggerArmed(false)
	, m_triggerOneShot(false)
	, m_sampleDepthValid(false)
	, m_sampleDepth(0)
	, m_rbwValid(false)
	, m_rbw(0)
{
	std::vector<string> info;
	string version =  ConverseSingle("version");
	if(version.empty())
	{
		LogError("Could not connect to TinySA :-/\n");
		return;
	}
	m_vendor= "tinySA";
	// Get model out of first line of info command response
	m_model= ConverseSingle("info");
	LogDebug("Model = %s\n",m_model.c_str());
	m_fwVersion = version;
	LogDebug("Version = %s\n",m_fwVersion.c_str());
	//Add spectrum view channel
	m_channels.push_back(
		new SpectrumChannel(
		this,
		string("CH1"),
		string("#ffff00"),
		m_channels.size()));
	// Get span information
}

TinySA::~TinySA()
{
}


size_t TinySA::ConverseMultiple(const std::string commandString, std::vector<string> &readLines)
{
	stringstream ss(ConverseString(commandString));
	string result;
	string curLine;
	bool firstLine = true;
	size_t size = 0;
    while (getline(ss, curLine)) 
	{
		if(firstLine)
		{
			//if(curLine.compare(commandString) != 0)
			//	LogWarning("Unexpected response '%s' to command string '%s'.\n",curLine.c_str(),commandString.c_str());
			firstLine = false;
		}
        else if (!curLine.empty()) 
		{
            readLines.push_back(curLine);
			size++;
        }
    }
	return size;
}

std::string TinySA::ConverseSingle(const std::string commandString)
{
	stringstream ss(ConverseString(commandString));
	string result;
	// Read first line (echo of command string)
	getline(ss,result);
	//if(result.compare(commandString) != 0)
	//	LogWarning("Unexpected response '%s' to command string '%s'.\n",result.c_str(),commandString.c_str());
	// Get second line as result
	getline(ss,result);
	return result;
}

std::string TinySA::ConverseString(const std::string commandString)
{
	string result = "";
	// Lock guard
	lock_guard<recursive_mutex> lock(m_transportMutex);
	m_transport->SendCommand(commandString+"\r\n");
	// Read untill we get  "ch>\r\n"
	char tmp = ' ';
	size_t bytesRead = 0;
	while(true)
	{	// Consume response until we find the end delimiter
		if(!m_transport->ReadRawData(1,(unsigned char*)&tmp))
			break;
		result += tmp;
		bytesRead++;
		if(bytesRead > MAX_RESPONSE_SIZE)
		{
			LogError("Error while reading data from TinySA: response too long (%zu bytes).\n",bytesRead);
			break;
		}
		if(result.size()>=TRAILER_STRING_LENGTH && (0 == result.compare (result.length() - TRAILER_STRING_LENGTH, TRAILER_STRING_LENGTH, TRAILER_STRING)))
			break;
	}
	//LogDebug("Received: %s\n",result.c_str());
	return result;
}

size_t TinySA::ConverseBinary(const std::string commandString, std::vector<uint8_t> &data)
{
	return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

///@brief Return the constant driver name string "tektronix"
string TinySA::GetDriverNameInternal()
{
	return "tiny-sa";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device interface functions

OscilloscopeChannel* TinySA::GetExternalTrigger()
{
	return NULL;
}

Oscilloscope::TriggerMode TinySA::PollTrigger()
{
	//Always report "triggered" so we can block on AcquireData() in ScopeThread
	return TRIGGER_MODE_TRIGGERED;
}

void TinySA::FlushConfigCache()
{
	SCPISA::FlushConfigCache();
	m_sampleDepthValid = false;
}

bool TinySA::AcquireData()
{
	//LogDebug("Acquiring data\n");

/*	map<int, vector<WaveformBase*> > pending_waveforms;

	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

			// Set source (before setting format)
			m_transport->SendCommandImmediate(string("DAT:SOU ") + GetOscilloscopeChannel(i)->GetHwname() + "_SV_NORMAL");

			//Select mode
			if(firstSpectrum || retry) // set again on retry
			{
				m_transport->SendCommandImmediate("DAT:WID 8");					//double precision floating point data
				m_transport->SendCommandImmediate("DAT:ENC SFPB");				//IEEE754 float
				firstSpectrum = false;
			}

			//Ask for the waveform preamble
			string preamble_str = m_transport->SendCommandImmediateWithReply("WFMO?", false);
			mso56_preamble preamble;

			//LogDebug("Channel %zu (%s)\n", nchan, m_channels[nchan]->GetHwname().c_str());
			//LogIndenter li2;

			//Process it
			if (!ReadPreamble(preamble_str, preamble))
				continue; // retry

			m_channelOffsets[i] = -preamble.yoff;

			//Read the data block
			size_t msglen;
			double* samples = (double*)m_transport->SendCommandImmediateWithRawBlockReply("CURV?", msglen);
			if(samples == NULL)
			{
				LogWarning("Didn't get any samples (timeout?)\n");

				ResynchronizeSCPI();

				continue; // retry
			}

			size_t nsamples = msglen/8;

			if (nsamples != (size_t)preamble.nr_pt)
			{
				LogWarning("Didn't get the right number of points\n");

				ResynchronizeSCPI();

				delete[] samples;

				continue; // retry
			}

			//Set up the capture we're going to store our data into
			//(no TDC data or fine timestamping available on Tektronix scopes?)
			auto cap = new UniformAnalogWaveform;
			cap->m_timescale = preamble.hzbase;
			cap->m_triggerPhase = 0;
			cap->m_startTimestamp = time(NULL);
			double t = GetTime();
			cap->m_startFemtoseconds = (t - floor(t)) * FS_PER_SECOND;
			cap->Resize(nsamples);
			cap->PrepareForCpuAccess();

			//We get dBm from the instrument, so just have to convert double to single precision
			//TODO: are other units possible here?
			//int64_t ibase = preamble.hzoff / preamble.hzbase;
			for(size_t j=0; j<nsamples; j++)
				cap->m_samples[j] = preamble.ymult*samples[j] + preamble.yoff;

			//Done, update the data
			cap->MarkSamplesModifiedFromCpu();
			pending_waveforms[nchan].push_back(cap);

			//Done
			delete[] samples;

			//Throw out garbage at the end of the message (why is this needed?)
			m_transport->ReadReply();

			//Look for peaks
			//TODO: make this configurable, for now 1 MHz spacing and up to 10 peaks
			dynamic_cast<SpectrumChannel*>(m_channels[nchan])->FindPeaks(cap, 10, 1000000);

	//Now that we have all of the pending waveforms, save them in sets across all channels
	m_pendingWaveformsMutex.lock();
	size_t num_pending = 1;	//TODO: segmented capture support
	for(size_t i=0; i<num_pending; i++)
	{
		SequenceSet s;
		for(size_t j=0; j<m_channels.size(); j++)
		{
			if(IsChannelEnabled(j))
				s[GetOscilloscopeChannel(j)] = pending_waveforms[j][i];
		}
		m_pendingWaveforms.push_back(s);
	}
	m_pendingWaveformsMutex.unlock();

	//Re-arm the trigger if not in one-shot mode
	if(!m_triggerOneShot)
	{
		lock_guard<recursive_mutex> lock3(m_cacheMutex);
		FlushChannelEnableStates();

		m_transport->SendCommandImmediate("ACQ:STATE ON");
		m_triggerArmed = true;
	}
*/
	//LogDebug("Acquisition done\n");
	return true;
}


void TinySA::Start()
{
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void TinySA::StartSingleTrigger()
{
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void TinySA::Stop()
{
	m_triggerArmed = false;
	m_triggerOneShot = false;
}

void TinySA::ForceTrigger()
{
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

bool TinySA::IsTriggerArmed()
{
	return m_triggerArmed;
}

vector<uint64_t> TinySA::GetSampleDepthsNonInterleaved()
{
	vector<uint64_t> ret;
	ret.push_back(51);
	ret.push_back(101);
	ret.push_back(145);
	ret.push_back(290);
	ret.push_back(500);
	ret.push_back(1000);
	ret.push_back(3000);
	ret.push_back(10000);
	ret.push_back(30000);
	return ret;
}

uint64_t TinySA::GetSampleDepth()
{
	if(!m_sampleDepthValid)
	{
		m_sampleDepth = 0;
		m_sampleDepthValid = true;
	}
	return m_sampleDepth;
}

void TinySA::SetSampleDepth(uint64_t depth)
{
}

void TinySA::PullTrigger()
{
	//pulling not needed, we always have a valid trigger cached
}

void TinySA::PushTrigger()
{
	//do nothing
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Spectrum analyzer mode

int64_t TinySA::GetResolutionBandwidth()
{
	return m_rbw;
}

void TinySA::SetResolutionBandwidth(int64_t rbw)
{	
	m_rbw = rbw;
	m_rbwValid = true;
	// TODO
}

void TinySA::SetSpan([[maybe_unused]] int64_t span)
{	// TODO
}

int64_t TinySA::GetSpan()
{	// TODO
	return 0;
}

void TinySA::SetCenterFrequency([[maybe_unused]] size_t channel,[[maybe_unused]] int64_t freq)
{	// TODO
}

int64_t TinySA::GetCenterFrequency([[maybe_unused]] size_t channel)
{	// TODO
	return 0;
}
