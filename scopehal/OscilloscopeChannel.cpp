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
	@brief Implementation of OscilloscopeChannel
 */

#include "scopehal.h"
#include "OscilloscopeChannel.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

OscilloscopeChannel::OscilloscopeChannel(
	Oscilloscope* scope,
	const string& hwname,
	const string& color,
	Unit xunit,
	size_t index)
	: InstrumentChannel(scope, hwname, color, xunit, index)
	, m_downloadState(DownloadState::DOWNLOAD_UNKNOWN)
	, m_downloadProgress(0.0)
	, m_downloadStartTime(0.0)
	, m_refcount(0)
{
}

OscilloscopeChannel::OscilloscopeChannel(
	Oscilloscope* scope,
	const string& hwname,
	const string& color,
	Unit xunit,
	Unit yunit,
	Stream::StreamType stype,
	size_t index)
	: InstrumentChannel(scope, hwname, color, xunit, yunit, stype, index)
	, m_downloadState(DownloadState::DOWNLOAD_UNKNOWN)
	, m_downloadProgress(0.0)
	, m_downloadStartTime(0.0)
	, m_refcount(0)
{
}

/**
	@brief Gives a channel a default display name if there's not one already.

	MUST NOT be called until the channel has been added to its parent scope.
 */
void OscilloscopeChannel::SetDefaultDisplayName()
{
	//If we have a scope, m_displayname is ignored.
	//Start out by pulling the name from hardware.
	//If it's not set, use our hardware name as the default.
	if(m_instrument)
	{
		auto name = GetScope()->GetChannelDisplayName(m_index);
		if(name == "")
			GetScope()->SetChannelDisplayName(m_index, m_hwname);
	}
}

OscilloscopeChannel::~OscilloscopeChannel()
{
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

float OscilloscopeChannel::GetOffset(size_t stream)
{
	if(GetScope() != NULL)
		return GetScope()->GetChannelOffset(m_index, stream);
	else
		return 0;
}

void OscilloscopeChannel::SetOffset(float offset, size_t stream)
{
	if(GetScope() != NULL)
		GetScope()->SetChannelOffset(m_index, stream, offset);
}

bool OscilloscopeChannel::IsEnabled()
{
	if(GetScope() != NULL)
		return GetScope()->IsChannelEnabled(m_index);
	else
		return true;
}

void OscilloscopeChannel::Enable()
{
	if(GetScope() != NULL)
		GetScope()->EnableChannel(m_index);
}

void OscilloscopeChannel::Disable()
{
	if(GetScope() != NULL)
		GetScope()->DisableChannel(m_index);
}

OscilloscopeChannel::CouplingType OscilloscopeChannel::GetCoupling()
{
	if(m_instrument)
		return GetScope()->GetChannelCoupling(m_index);
	else
		return OscilloscopeChannel::COUPLE_SYNTHETIC;
}

vector<OscilloscopeChannel::CouplingType> OscilloscopeChannel::GetAvailableCouplings()
{
	if(m_instrument)
		return GetScope()->GetAvailableCouplings(m_index);
	else
	{
		vector<OscilloscopeChannel::CouplingType> ret;
		ret.push_back(COUPLE_SYNTHETIC);
		return ret;
	}
}

void OscilloscopeChannel::SetCoupling(CouplingType type)
{
	if(m_instrument)
		GetScope()->SetChannelCoupling(m_index, type);
}

double OscilloscopeChannel::GetAttenuation()
{
	if(m_instrument)
		return GetScope()->GetChannelAttenuation(m_index);
	else
		return 1;
}

void OscilloscopeChannel::SetAttenuation(double atten)
{
	if(m_instrument)
		GetScope()->SetChannelAttenuation(m_index, atten);
}

int OscilloscopeChannel::GetBandwidthLimit()
{
	if(m_instrument)
		return GetScope()->GetChannelBandwidthLimit(m_index);
	else
		return 0;
}

void OscilloscopeChannel::SetBandwidthLimit(int mhz)
{
	if(m_instrument)
		GetScope()->SetChannelBandwidthLimit(m_index, mhz);
}

float OscilloscopeChannel::GetVoltageRange(size_t stream)
{
	if(m_instrument)
		return GetScope()->GetChannelVoltageRange(m_index, stream);
	else
		return 1;	//TODO: get from input
}

void OscilloscopeChannel::SetVoltageRange(float range, size_t stream)
{
	if(m_instrument)
		return GetScope()->SetChannelVoltageRange(m_index, stream, range);
}

void OscilloscopeChannel::SetDeskew(int64_t skew)
{
	if(m_instrument)
		GetScope()->SetDeskewForChannel(m_index, skew);
}

int64_t OscilloscopeChannel::GetDeskew()
{
	if(m_instrument)
		return GetScope()->GetDeskewForChannel(m_index);
	return 0;
}

void OscilloscopeChannel::SetDigitalHysteresis(float level)
{
	if(m_instrument)
		GetScope()->SetDigitalHysteresis(m_index, level);
}

void OscilloscopeChannel::SetDigitalThreshold(float level)
{
	if(m_instrument)
		GetScope()->SetDigitalThreshold(m_index, level);
}

void OscilloscopeChannel::SetCenterFrequency(int64_t freq)
{
	if(m_instrument)
		GetScope()->SetCenterFrequency(m_index, freq);
}

void OscilloscopeChannel::SetDisplayName(string name)
{
	if(m_instrument)
		GetScope()->SetChannelDisplayName(m_index, name);
	InstrumentChannel::SetDisplayName(name);
}

string OscilloscopeChannel::GetDisplayName()
{
	//Use cached name if we have it
	auto cached = InstrumentChannel::GetDisplayName();
	if(!cached.empty())
		return cached;

	//If not, pull from hardware
	if(m_instrument)
	{
		auto tmp = GetScope()->GetChannelDisplayName(m_index);
		InstrumentChannel::SetDisplayName(tmp);
		return tmp;
	}

	//No hardware? just use hwname
	else
		return m_hwname;
}

bool OscilloscopeChannel::CanInvert()
{
	if(m_instrument)
		return GetScope()->CanInvert(m_index);
	else
		return false;
}

void OscilloscopeChannel::Invert(bool invert)
{
	if(m_instrument)
		GetScope()->Invert(m_index, invert);
}

bool OscilloscopeChannel::IsInverted()
{
	if(m_instrument)
		return GetScope()->IsInverted(m_index);
	else
		return false;
}

void OscilloscopeChannel::AutoZero()
{
	if(m_instrument)
		GetScope()->AutoZero(m_index);
}

bool OscilloscopeChannel::CanAutoZero()
{
	if(m_instrument)
		return GetScope()->CanAutoZero(m_index);
	else
		return false;
}

void OscilloscopeChannel::Degauss()
{
	if(m_instrument)
		GetScope()->Degauss(m_index);
}

bool OscilloscopeChannel::CanDegauss()
{
	if(m_instrument)
		return GetScope()->CanDegauss(m_index);
	else
		return false;
}

string OscilloscopeChannel::GetProbeName()
{
	if(m_instrument)
		return GetScope()->GetProbeName(m_index);
	else
		return "";
}

bool OscilloscopeChannel::HasInputMux()
{
	if(m_instrument)
		return GetScope()->HasInputMux(m_index);
	return false;
}

size_t OscilloscopeChannel::GetInputMuxSetting()
{
	if(m_instrument)
		return GetScope()->GetInputMuxSetting(m_index);
	return 0;
}

void OscilloscopeChannel::SetInputMux(size_t select)
{
	if(m_instrument)
		GetScope()->SetInputMux(m_index, select);
}

InstrumentChannel::DownloadState OscilloscopeChannel::GetDownloadState()
{
	return m_downloadState;
}

float OscilloscopeChannel::GetDownloadProgress()
{
	return m_downloadProgress;
}

double OscilloscopeChannel::GetDownloadStartTime()
{
	return m_downloadStartTime;
}
