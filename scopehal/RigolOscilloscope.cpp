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

#include "scopehal.h"
#include "RigolOscilloscope.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

RigolOscilloscope::RigolOscilloscope()
{
	//nothing in base class
}

RigolOscilloscope::~RigolOscilloscope()
{
}

/**
	@brief Connect to the scope and figure out what's going on
 */
void RigolOscilloscope::SharedCtorInit()
{
	//Ask for the ID
	SendCommand("*IDN?");
	string reply = ReadReply();
	char vendor[128] = "";
	char model[128] = "";
	char serial[128] = "";
	char version[128] = "";
	if(4 != sscanf(reply.c_str(), "%127[^,],%127[^,],%127[^,],%127s", vendor, model, serial, version))
	{
		LogError("Bad IDN response %s\n", reply.c_str());
		return;
	}
	m_vendor = vendor;
	m_model = model;
	m_serial = serial;
	m_fwVersion = version;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string RigolOscilloscope::GetName()
{
	return m_model;
}

string RigolOscilloscope::GetVendor()
{
	return m_vendor;
}

string RigolOscilloscope::GetSerial()
{
	return m_serial;
}

unsigned int RigolOscilloscope::GetInstrumentTypes()
{
	return Instrument::INST_OSCILLOSCOPE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TODO

void RigolOscilloscope::FlushConfigCache()
{

}

bool RigolOscilloscope::IsChannelEnabled(size_t i)
{

}

void RigolOscilloscope::EnableChannel(size_t i)
{

}

void RigolOscilloscope::DisableChannel(size_t i)
{

}

OscilloscopeChannel::CouplingType RigolOscilloscope::GetChannelCoupling(size_t i)
{

}

void RigolOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{

}

double RigolOscilloscope::GetChannelAttenuation(size_t i)
{

}

void RigolOscilloscope::SetChannelAttenuation(size_t i, double atten)
{

}

int RigolOscilloscope::GetChannelBandwidthLimit(size_t i)
{

}

void RigolOscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
{

}

double RigolOscilloscope::GetChannelVoltageRange(size_t i)
{

}

void RigolOscilloscope::SetChannelVoltageRange(size_t i, double range)
{

}

OscilloscopeChannel* RigolOscilloscope::GetExternalTrigger()
{

}

double RigolOscilloscope::GetChannelOffset(size_t i)
{

}

void RigolOscilloscope::SetChannelOffset(size_t i, double offset)
{

}

void RigolOscilloscope::ResetTriggerConditions()
{

}

Oscilloscope::TriggerMode RigolOscilloscope::PollTrigger()
{

}

bool RigolOscilloscope::AcquireData(bool toQueue)
{

}

void RigolOscilloscope::Start()
{

}

void RigolOscilloscope::StartSingleTrigger()
{

}

void RigolOscilloscope::Stop()
{

}

bool RigolOscilloscope::IsTriggerArmed()
{

}

size_t RigolOscilloscope::GetTriggerChannelIndex()
{

}

void RigolOscilloscope::SetTriggerChannelIndex(size_t i)
{

}

float RigolOscilloscope::GetTriggerVoltage()
{

}

void RigolOscilloscope::SetTriggerVoltage(float v)
{

}

Oscilloscope::TriggerType RigolOscilloscope::GetTriggerType()
{

}

void RigolOscilloscope::SetTriggerType(Oscilloscope::TriggerType type)
{

}

void RigolOscilloscope::SetTriggerForChannel(OscilloscopeChannel* channel, vector<TriggerType> triggerbits)
{

}

vector<uint64_t> RigolOscilloscope::GetSampleRatesNonInterleaved()
{
}

vector<uint64_t> RigolOscilloscope::GetSampleRatesInterleaved()
{
}

set<Oscilloscope::InterleaveConflict> RigolOscilloscope::GetInterleaveConflicts()
{
}
vector<uint64_t> RigolOscilloscope::GetSampleDepthsNonInterleaved()
{
}
vector<uint64_t> RigolOscilloscope::GetSampleDepthsInterleaved()
{
}
