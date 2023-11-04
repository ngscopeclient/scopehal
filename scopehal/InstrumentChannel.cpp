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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Implementation of InstrumentChannel
 */

#include "scopehal.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

InstrumentChannel::InstrumentChannel(
	const string& hwname,
	const string& color,
	Unit xunit,
	size_t index)
	: m_displaycolor(color)
	, m_hwname(hwname)
	, m_displayname(hwname)
	, m_index(index)
	, m_xAxisUnit(xunit)
{
}

InstrumentChannel::InstrumentChannel(
	const string& hwname,
	const string& color,
	Unit xunit,
	Unit yunit,
	Stream::StreamType stype,
	size_t index)
	: m_displaycolor(color)
	, m_hwname(hwname)
	, m_displayname(hwname)
	, m_index(index)
	, m_xAxisUnit(xunit)
{
	AddStream(yunit, "data", stype);
}

InstrumentChannel::~InstrumentChannel()
{
	ClearStreams();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

InstrumentChannel::PhysicalConnector InstrumentChannel::GetPhysicalConnector()
{
	return CONNECTOR_BNC;
}

/**
	@brief Sets the human-readable nickname for this channel, as displayed in the GUI
 */
void InstrumentChannel::SetDisplayName(string name)
{
	m_displayname = name;
}

/**
	@brief Gets the human-readable nickname for this channel, as displayed in the GUI
 */
string InstrumentChannel::GetDisplayName()
{
	return m_displayname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Stream management

/**
	@brief Clears out any existing streams
 */
void InstrumentChannel::ClearStreams()
{
	for(auto s : m_streams)
		delete s.m_waveform;
	m_streams.clear();
}

/**
	@brief Adds a new data stream to the channel

	@return Index of the new stream
 */
size_t InstrumentChannel::AddStream(Unit yunit, const string& name, Stream::StreamType stype, uint8_t flags)
{
	size_t index = m_streams.size();
	m_streams.push_back(Stream(yunit, name, stype, flags));
	return index;
}

/**
	@brief Sets the waveform data for a given stream, replacing any previous waveform.

	Calling this function with pNew == GetData() is a legal no-op.

	Any existing waveform is deleted, unless it is the same as pNew.
 */
void InstrumentChannel::SetData(WaveformBase* pNew, size_t stream)
{
	if(m_streams[stream].m_waveform == pNew)
		return;

	if(m_streams[stream].m_waveform != NULL)
		delete m_streams[stream].m_waveform;
	m_streams[stream].m_waveform = pNew;
}

bool InstrumentChannel::ShouldPersistWaveform()
{
	//Default to persisting everything
	return true;
}
