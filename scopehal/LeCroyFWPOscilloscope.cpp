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
#include "LeCroyFWPOscilloscope.h"

using namespace std;

#pragma pack(push, 4)
struct WaveformHeader
{
	int32_t version;
	int32_t flags;
	int32_t headerSize;
	int32_t windowSize;
	int32_t numSamples;
	int32_t segmentIndex;
	int32_t numSweeps;
	int32_t reserved;
	double verticalGain;
	double verticalOffset;
	double verticalResolution;
	double horizontalInterval;
	double horizontalOffset;
	double horizontalResolution;
	int64_t trigTime;
	char verticalUnit[48];
	char horizontalUnit[48];
};
#pragma pack(pop)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

LeCroyFWPOscilloscope::LeCroyFWPOscilloscope(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, LeCroyOscilloscope(transport)
	, m_fallback(false)
	, m_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)
{
	auto vtransport = dynamic_cast<VICPSocketTransport*>(transport);

	//Make sure we have the right options
	if(!m_hasXdev)
	{
		LogWarning("LeCroyFWPOscilloscope driver requires instrument to have the XDEV option installed to use FastWavePort functionality.\n");
		LogWarning("Falling back to SCPI waveform download using LeCroyOscilloscope driver.\n");
		m_fallback = true;
	}

	//Make sure transport is VICP
	else if(!vtransport)
	{
		LogWarning("LeCroyFWPOscilloscope driver is only compatible with VICP transport.\n");
		LogWarning("Falling back to SCPI waveform download using LeCroyOscilloscope driver.\n");
		m_fallback = true;
	}

	//Attempt to connect to the data socket
	else if(!m_socket.Connect(vtransport->GetHostname(), 1862))
	{
		LogWarning("Failed to connect to scopehal-fwp-bridge server at %s:1862\n", vtransport->GetHostname().c_str());
		LogWarning("Falling back to SCPI waveform download using LeCroyOscilloscope driver.\n");
		m_fallback = true;
	}

	else if(!m_socket.DisableNagle())
	{
		LogWarning("Failed to disable Nagle on data socket\n");
		LogWarning("Falling back to SCPI waveform download using LeCroyOscilloscope driver.\n");
		m_fallback = true;
	}

	//TODO: configure math functions... do we want to use F1-F4? should we use F9-F12 to minimize chance of conflict?

	LogDebug("all good, connected\n");
}

LeCroyFWPOscilloscope::~LeCroyFWPOscilloscope()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Object creation / enumeration

string LeCroyFWPOscilloscope::GetDriverNameInternal()
{
	return "lecroy_fwp";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering

Oscilloscope::TriggerMode LeCroyFWPOscilloscope::PollTrigger()
{
	if(m_fallback)
		return LeCroyOscilloscope::PollTrigger();

	//Normal operation, return "triggered" so the scpi thread blocks on waveform download
	else
		return TRIGGER_MODE_TRIGGERED;
}

void LeCroyFWPOscilloscope::Start()
{
	if(m_fallback)
		LeCroyOscilloscope::Start();

	//We can use actual "normal" triggering since the FastWavePort manages sync!
	m_transport->SendCommandQueued("TRIG_MODE NORMAL");
	m_transport->FlushCommandQueue();
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

bool LeCroyFWPOscilloscope::AcquireData()
{
	if(m_fallback)
		return LeCroyOscilloscope::AcquireData();

	//TODO: implement digital channels
	//For now we're ignoring that as my SDA816 doesn't have a MSO probe and my WaveRunner doesn't have XDEV...

	//TODO: sequence mode support

	//For now, hard code four channels as we don't have this part synced yet
	//No need to lock transport mutex as this is a dedicated socket
	WaveformHeader headers[4];
	vector<int16_t> data[4];
	map<int, WaveformBase* > pending_waveforms;
	for(int i=0; i<4; i++)
	{
		//Grab the waveform header
		if(!m_socket.RecvLooped((uint8_t*)&headers[i], sizeof(WaveformHeader)))
			return false;
	}

	for(int i=0; i<4; i++)
	{
		//Grab the data
		data[i].resize(headers[i].numSamples);
		if(!m_socket.RecvLooped((uint8_t*)&data[i][0], headers[i].numSamples * sizeof(int16_t)))
			return false;
	}

	//Convert the waveforms
	for(int i=0; i<4; i++)
	{
		auto wfm = AllocateAnalogWaveform(m_nickname + "." + GetChannel(i)->GetHwname());
		wfm->m_timescale = round(headers[i].horizontalInterval * FS_PER_SECOND);
		wfm->m_triggerPhase = fmodf(headers[i].horizontalOffset, wfm->m_timescale);

		//FIXME
		//Timestamp is seconds since jan 1 2000 at midnight *local time*
		wfm->m_startTimestamp = 0;
		wfm->m_startFemtoseconds = 0;

		//Crunch the data
		wfm->Resize(headers[i].numSamples);
		Convert16BitSamples(
			wfm->m_samples.GetCpuPointer(),
			&data[i][0],
			headers[i].verticalGain,
			headers[i].verticalOffset,
			headers[i].numSamples);
		wfm->MarkSamplesModifiedFromCpu();

		pending_waveforms[i] = wfm;
	}

	//Now that we have all of the pending waveforms, save them in sets across all channels
	m_pendingWaveformsMutex.lock();
		SequenceSet s;
		for(size_t j=0; j<m_channels.size(); j++)
		{
			if(pending_waveforms.find(j) != pending_waveforms.end())
				s[m_channels[j]] = pending_waveforms[j];
		}
		m_pendingWaveforms.push_back(s);
	m_pendingWaveformsMutex.unlock();

	return true;
}
