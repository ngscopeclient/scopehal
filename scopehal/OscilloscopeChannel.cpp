/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2020 Andrew D. Zonenberg                                                                          *
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
	@brief Implementation of OscilloscopeChannel
 */

#include "scopehal.h"
#include "OscilloscopeChannel.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

OscilloscopeChannel::OscilloscopeChannel(
	Oscilloscope* scope,
	string hwname,
	OscilloscopeChannel::ChannelType type,
	string color,
	int width,
	size_t index,
	bool physical)
	: m_displaycolor(color)
	, m_displayname(hwname)
	, m_scope(scope)
	, m_type(type)
	, m_hwname(hwname)
	, m_width(width)
	, m_index(index)
	, m_physical(physical)
	, m_refcount(0)
	, m_xAxisUnit(Unit::UNIT_PS)
	, m_yAxisUnit(Unit::UNIT_VOLTS)
{
	//Create a stream for our output.
	//Normal channels only have one stream.
	//Special instruments like SDRs with complex output, or filters/decodes, can have arbitrarily many.
	AddStream("data");
}

OscilloscopeChannel::~OscilloscopeChannel()
{
	for(auto p : m_streamData)
		delete p;
	m_streamData.clear();
	m_streamNames.clear();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helpers for calling scope functions

void OscilloscopeChannel::AddRef()
{
	if(m_refcount == 0)
		Enable();
	m_refcount ++;
}

void OscilloscopeChannel::Release()
{
	m_refcount --;
	if(m_refcount == 0)
		Disable();
}

double OscilloscopeChannel::GetOffset()
{
	if(m_scope != NULL)
		return m_scope->GetChannelOffset(m_index);
	else
		return 0;
}

void OscilloscopeChannel::SetOffset(double offset)
{
	if(m_scope != NULL)
		m_scope->SetChannelOffset(m_index, offset);
}

bool OscilloscopeChannel::IsEnabled()
{
	if(m_scope != NULL)
		return m_scope->IsChannelEnabled(m_index);
	else
		return true;
}

void OscilloscopeChannel::Enable()
{
	if(m_scope != NULL)
		m_scope->EnableChannel(m_index);
}

void OscilloscopeChannel::Disable()
{
	if(m_scope != NULL)
		m_scope->DisableChannel(m_index);
}

OscilloscopeChannel::CouplingType OscilloscopeChannel::GetCoupling()
{
	if(m_scope)
		return m_scope->GetChannelCoupling(m_index);
	else
		return OscilloscopeChannel::COUPLE_SYNTHETIC;
}

void OscilloscopeChannel::SetCoupling(CouplingType type)
{
	if(m_scope)
		m_scope->SetChannelCoupling(m_index, type);
}

double OscilloscopeChannel::GetAttenuation()
{
	if(m_scope)
		return m_scope->GetChannelAttenuation(m_index);
	else
		return 1;
}

void OscilloscopeChannel::SetAttenuation(double atten)
{
	if(m_scope)
		m_scope->SetChannelAttenuation(m_index, atten);
}

int OscilloscopeChannel::GetBandwidthLimit()
{
	if(m_scope)
		return m_scope->GetChannelBandwidthLimit(m_index);
	else
		return 0;
}

void OscilloscopeChannel::SetBandwidthLimit(int mhz)
{
	if(m_scope)
		m_scope->SetChannelBandwidthLimit(m_index, mhz);
}

double OscilloscopeChannel::GetVoltageRange()
{
	if(m_scope)
		return m_scope->GetChannelVoltageRange(m_index);
	else
		return 1;	//TODO: get from input
}

void OscilloscopeChannel::SetVoltageRange(double range)
{
	if(m_scope)
		return m_scope->SetChannelVoltageRange(m_index, range);
}

void OscilloscopeChannel::SetDeskew(int64_t skew)
{
	if(m_scope)
		m_scope->SetDeskewForChannel(m_index, skew);
}

int64_t OscilloscopeChannel::GetDeskew()
{
	if(m_scope)
		return m_scope->GetDeskewForChannel(m_index);
	return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void OscilloscopeChannel::SetData(WaveformBase* pNew, size_t stream)
{
	if(m_streamData[stream] == pNew)
		return;

	if(m_streamData[stream] != NULL)
		delete m_streamData[stream];
	m_streamData[stream] = pNew;
}
