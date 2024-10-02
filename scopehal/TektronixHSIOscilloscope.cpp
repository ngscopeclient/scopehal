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
	@brief Implementation of TektronixHSIOscilloscope

	@ingroup scopedrivers
 */

#include "scopehal.h"
#include "TektronixHSIOscilloscope.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initialize the driver

	@param transport	SCPITwinLanTransport pointing at the bridge server
 */
TektronixHSIOscilloscope::TektronixHSIOscilloscope(SCPITransport* transport)
	   : SCPIDevice(transport)
	   , SCPIInstrument(transport)
	   , TektronixOscilloscope(transport)
{
	if (m_family != FAMILY_MSO5)
		LogWarning("TektronixHSIOscilloscope only tested on MSO5\n");

	auto csock = dynamic_cast<SCPITwinLanTransport*>(m_transport);
	if(!csock)
		LogFatal("TektronixHSIOscilloscope expects a SCPITwinLanTransport\n");

	// greeting = transport.ReadRawData()
}

TektronixHSIOscilloscope::~TektronixHSIOscilloscope()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string TektronixHSIOscilloscope::GetDriverNameInternal()
{
	return "tektronix.hsi";
}

Oscilloscope::TriggerMode TektronixHSIOscilloscope::PollTrigger()
{
	return m_triggerArmed ? TRIGGER_MODE_TRIGGERED : TRIGGER_MODE_STOP;
}

bool TektronixHSIOscilloscope::AcquireData()
{
	if (!m_triggerArmed)
		return true;

	//uint8_t r = 'K';
	//m_transport->SendRawData(1, &r);

	/*
	uint64_t num_samples;
	m_transport->ReadRawData(sizeof(num_samples), (uint8_t*)&num_samples);

	uint64_t sr_fs;
	m_transport->ReadRawData(sizeof(sr_fs), (uint8_t*)&sr_fs);

	LogTrace("About to recv %ld floats\n", num_samples);

	SequenceSet s;
	UniformAnalogWaveform* cap = AllocateAnalogWaveform(m_nickname + "." + GetChannel(0)->GetHwname());
	cap->m_timescale = sr_fs;
	cap->m_triggerPhase = 0;
	cap->m_startTimestamp = time(NULL);
	cap->m_startFemtoseconds = 0;

	cap->Resize(num_samples);
	cap->PrepareForCpuAccess();
	m_transport->ReadRawData(num_samples * sizeof(float), (uint8_t*)&cap->m_samples[0]);
	cap->MarkModifiedFromCpu();

	s[GetOscilloscopeChannel(0)] = cap;

	m_pendingWaveformsMutex.lock();
	m_pendingWaveforms.push_back(s);

	while (m_pendingWaveforms.size() > 2)
	{
		SequenceSet set = *m_pendingWaveforms.begin();
		for(auto it : set)
			delete it.second;
		m_pendingWaveforms.pop_front();
	}

	m_pendingWaveformsMutex.unlock();

	if (m_triggerOneShot)
		m_triggerArmed = false;
	*/
	return true;
}

void TektronixHSIOscilloscope::Start()
{
	//Flush enable states with the cache mutex locked.
	//This is necessary to ensure the scope's view of what's enabled is consistent with ours at trigger time.
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());
	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	FlushChannelEnableStates();

	m_transport->SendCommandQueued("ACQ:STOPA RUNST");
	m_transport->SendCommandQueued("ACQ:STATE ON");
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void TektronixHSIOscilloscope::StartSingleTrigger()
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());
	lock_guard<recursive_mutex> lock2(m_cacheMutex);
	FlushChannelEnableStates();

	m_transport->SendCommandQueued("ACQ:STOPA SEQ");
	m_transport->SendCommandQueued("ACQ:STATE ON");
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void TektronixHSIOscilloscope::Stop()
{
	m_triggerArmed = false;
	m_transport->SendCommandQueued("ACQ:STATE STOP");
	m_triggerOneShot = true;
}
