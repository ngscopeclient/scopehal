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
{
}

CopperMountainVNA::~CopperMountainVNA()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device enumeration

string CopperMountainVNA::GetDriverNameInternal()
{
	return "coppermt";
}

unsigned int CopperMountainVNA::GetInstrumentTypes()
{
	return Instrument::INST_OSCILLOSCOPE;
}

uint32_t CopperMountainVNA::GetInstrumentTypesForChannel(size_t /*i*/)
{
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Driver logic

bool CopperMountainVNA::IsChannelEnabled(size_t i)
{
}

void CopperMountainVNA::EnableChannel(size_t i)
{
}

void CopperMountainVNA::DisableChannel(size_t i)
{
}

OscilloscopeChannel::CouplingType CopperMountainVNA::GetChannelCoupling(size_t /*i*/)
{
	//all inputs are ac coupled 50 ohm impedance
	return OscilloscopeChannel::COUPLE_AC_50;
}

void CopperMountainVNA::SetChannelCoupling(size_t /*i*/, OscilloscopeChannel::CouplingType /*type*/)
{
	//no-op, coupling cannot be changed
}

vector<OscilloscopeChannel::CouplingType> CopperMountainVNA::GetAvailableCouplings(size_t i)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	ret.push_back(OscilloscopeChannel::COUPLE_AC_50);
	return ret;
}

double CopperMountainVNA::GetChannelAttenuation(size_t /*i*/)
{
	return 1;
}

void CopperMountainVNA::SetChannelAttenuation(size_t /*i*/, double /*atten*/)
{
	//no-op
}

unsigned int CopperMountainVNA::GetChannelBandwidthLimit(size_t /*i*/)
{
	return 0;
}

void CopperMountainVNA::SetChannelBandwidthLimit(size_t /*i*/, unsigned int /*limit_mhz*/)
{
	//no-op
}

float CopperMountainVNA::GetChannelVoltageRange(size_t i, size_t stream)
{
}

void CopperMountainVNA::SetChannelVoltageRange(size_t i, size_t stream, float range)
{
}

float CopperMountainVNA::GetChannelOffset(size_t i, size_t stream)
{
}

void CopperMountainVNA::SetChannelOffset(size_t i, size_t stream, float offset)
{
}

//TODO: support ext trig if any
OscilloscopeChannel* CopperMountainVNA::GetExternalTrigger()
{
	return nullptr;
}

Oscilloscope::TriggerMode CopperMountainVNA::PollTrigger()
{
}

void CopperMountainVNA::Start()
{
}

void CopperMountainVNA::StartSingleTrigger()
{
}

void CopperMountainVNA::Stop()
{
}

void CopperMountainVNA::ForceTrigger()
{
}

bool CopperMountainVNA::IsTriggerArmed()
{
}

void CopperMountainVNA::PushTrigger()
{
}

void CopperMountainVNA::PullTrigger()
{
}

vector<string> CopperMountainVNA::GetTriggerTypes()
{

}

bool CopperMountainVNA::AcquireData()
{
	return true;
}

vector<uint64_t> CopperMountainVNA::GetSampleRatesNonInterleaved()
{
}

vector<uint64_t> CopperMountainVNA::GetSampleRatesInterleaved()
{
}

set<Oscilloscope::InterleaveConflict> CopperMountainVNA::GetInterleaveConflicts()
{
}

vector<uint64_t> CopperMountainVNA::GetSampleDepthsNonInterleaved()
{
}

vector<uint64_t> CopperMountainVNA::GetSampleDepthsInterleaved()
{
}

uint64_t CopperMountainVNA::GetSampleRate()
{
}

uint64_t CopperMountainVNA::GetSampleDepth()
{
}

void CopperMountainVNA::SetSampleDepth(uint64_t depth)
{
}

void CopperMountainVNA::SetSampleRate(uint64_t rate)
{
}

void CopperMountainVNA::SetTriggerOffset(int64_t offset)
{
}

int64_t CopperMountainVNA::GetTriggerOffset()
{
}

bool CopperMountainVNA::IsInterleaving()
{
}

bool CopperMountainVNA::SetInterleaving(bool combine)
{
}
