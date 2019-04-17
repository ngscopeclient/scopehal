/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2019 Andrew D. Zonenberg                                                                          *
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
	@brief Implementation of Oscilloscope
 */

#include "scopehal.h"
#include "OscilloscopeChannel.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

Oscilloscope::Oscilloscope()
{

}

Oscilloscope::~Oscilloscope()
{
	for(size_t i=0; i<m_channels.size(); i++)
		delete m_channels[i];
	m_channels.clear();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device properties

void Oscilloscope::FlushConfigCache()
{
	//nothing to do, base class has no caching
}

size_t Oscilloscope::GetChannelCount()
{
	return m_channels.size();
}

OscilloscopeChannel* Oscilloscope::GetChannel(size_t i)
{
	if(i < m_channels.size())
		return m_channels[i];
	else
		return NULL;
}

OscilloscopeChannel* Oscilloscope::GetChannelByDisplayName(string name)
{
	for(size_t i=0; i<m_channels.size(); i++)
	{
		if(m_channels[i]->m_displayname == name)
			return m_channels[i];
	}
	return NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering helpers

bool Oscilloscope::WaitForTrigger(int timeout)
{
	bool trig = false;
	for(int i=0; i<timeout*100 && !trig; i++)
	{
		trig = (PollTriggerFifo() == Oscilloscope::TRIGGER_MODE_TRIGGERED);
		usleep(10 * 1000);
	}

	return trig;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sequenced capture

/**
	@brief Just like PollTrigger(), but checks if we have pending data in the sequence buffer first
 */
Oscilloscope::TriggerMode Oscilloscope::PollTriggerFifo()
{
	if(m_pendingWaveforms.size())
		return Oscilloscope::TRIGGER_MODE_TRIGGERED;
	else
		return PollTrigger();
}

/**
	@brief Just like AcquireData(), but checks if we have pending data in the sequence buffer first
 */
bool Oscilloscope::AcquireDataFifo(sigc::slot1<int, float> progress_callback)
{
	if(m_pendingWaveforms.size())
	{
		SequenceSet set = *m_pendingWaveforms.begin();
		for(auto it : set)
			it.first->SetData(it.second);
		m_pendingWaveforms.pop_front();
		return true;
	}
	else
		return AcquireData(progress_callback);
}
