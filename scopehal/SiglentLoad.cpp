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
#include "SiglentLoad.h"
#include "LoadChannel.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SiglentLoad::SiglentLoad(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
{
	m_channels.push_back(new LoadChannel("Load", "#808080", 0));

	//Populate the cache for a few commonly used variables
	GetLoadModeUncached(0);
}

SiglentLoad::~SiglentLoad()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// System info / configuration

string SiglentLoad::GetDriverNameInternal()
{
	return "siglent_load";
}

unsigned int SiglentLoad::GetInstrumentTypes()
{
	return INST_LOAD;
}

string SiglentLoad::GetName()
{
	return m_model;
}

string SiglentLoad::GetVendor()
{
	return m_vendor;
}

string SiglentLoad::GetSerial()
{
	return m_serial;
}

uint32_t SiglentLoad::GetInstrumentTypesForChannel(size_t i)
{
	if(i == 0)
		return Instrument::INST_LOAD;
	else
		return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Load

/*
	Get waveform (200 points, at what rate?)
	MEAS:WAVE:VOLT
	MEAS:WAVE:CURR

	Short circuit mode TODO
	Transient mode TODO
	List/sequence mode TODO
 */

Load::LoadMode SiglentLoad::GetLoadMode(size_t /*channel*/)
{
	return m_modeCached;
}

Load::LoadMode SiglentLoad::GetLoadModeUncached(size_t /*channel*/)
{
	auto reply = Trim(m_transport->SendCommandQueuedWithReply("SOUR:FUNC?"));
	if(reply == "CURRENT")
		return MODE_CONSTANT_CURRENT;
	else if(reply == "VOLTAGE")
		return MODE_CONSTANT_VOLTAGE;
	else if(reply == "POWER")
		return MODE_CONSTANT_POWER;
	else if(reply == "RESISTANCE")
		return MODE_CONSTANT_RESISTANCE;
	//TODO: LED mode

	LogWarning("[SiglentLoad::GetLoadMode] Unknown mode %s\n", reply.c_str());
	return MODE_CONSTANT_CURRENT;
}

void SiglentLoad::SetLoadMode(size_t /*channel*/, LoadMode mode)
{
	switch(mode)
	{
		case MODE_CONSTANT_CURRENT:
			m_transport->SendCommandQueued("SOUR:FUNC CURR");
			break;

		case MODE_CONSTANT_POWER:
			m_transport->SendCommandQueued("SOUR:FUNC POW");
			break;

		case MODE_CONSTANT_RESISTANCE:
			m_transport->SendCommandQueued("SOUR:FUNC RES");
			break;

		case MODE_CONSTANT_VOLTAGE:
			m_transport->SendCommandQueued("SOUR:FUNC VOLT");
			break;

		default:
			LogWarning("[SiglentLoad::SetLoadMode] Unknown mode %d\n", mode);
			break;
	}

	m_modeCached = mode;
}

vector<float> SiglentLoad::GetLoadCurrentRanges(size_t /*channel*/)
{
	vector<float> ranges;
	ranges.push_back(5);
	ranges.push_back(30);
	return ranges;
}

size_t SiglentLoad::GetLoadCurrentRange(size_t channel)
{
	int maxcur = 0;
	auto mode = GetLoadMode(channel);
	switch(mode)
	{
		case MODE_CONSTANT_CURRENT:
			maxcur = stoi(Trim(m_transport->SendCommandQueuedWithReply("SOUR:CURR:IRANG?")));
			break;

		case MODE_CONSTANT_VOLTAGE:
			maxcur = stoi(Trim(m_transport->SendCommandQueuedWithReply("SOUR:VOLT:IRANG?")));
			break;

		default:
			LogWarning("[SiglentLoad::GetLoadCurrentRange] Unknown mode %d\n", mode);
			break;
	}

	//Given the current value, find which range we're in
	if(maxcur > 5)
		return 1;
	else
		return 0;
}

vector<float> SiglentLoad::GetLoadVoltageRanges(size_t /*channel*/)
{
	vector<float> ranges;
	ranges.push_back(36);
	ranges.push_back(150);
	return ranges;
}

size_t SiglentLoad::GetLoadVoltageRange(size_t channel)
{
	int maxvolt = 0;
	auto mode = GetLoadMode(channel);
	switch(mode)
	{
		case MODE_CONSTANT_CURRENT:
			maxvolt = stoi(Trim(m_transport->SendCommandQueuedWithReply("SOUR:CURR:VRANG?")));
			break;

		case MODE_CONSTANT_VOLTAGE:
			maxvolt = stoi(Trim(m_transport->SendCommandQueuedWithReply("SOUR:VOLT:VRANG?")));
			break;

		default:
			LogWarning("[SiglentLoad::GetLoadVoltageRange] Unknown mode %d\n", mode);
			break;
	}

	//Given the voltage value, find which range we're in
	if(maxvolt > 36)
		return 1;
	else
		return 0;
}

void SiglentLoad::SetLoadVoltageRange(size_t channel, size_t rangeIndex)
{
	auto ranges = GetLoadVoltageRanges(channel);
	int fullScaleRange = ranges[rangeIndex];

	//Cannot change range while load is enabled
	bool wasOn = GetLoadActive(channel);
	if(wasOn)
		SetLoadActive(channel, false);

	auto mode = GetLoadMode(channel);
	switch(mode)
	{
		case MODE_CONSTANT_CURRENT:
			m_transport->SendCommandQueued(string("SOUR:CURR:VRANG ") + to_string(fullScaleRange));
			break;

		case MODE_CONSTANT_VOLTAGE:
			m_transport->SendCommandQueued(string("SOUR:VOLT:VRANG ") + to_string(fullScaleRange));
			break;

		default:
			LogWarning("[SiglentLoad::SetLoadVoltageRange] Unknown mode %d\n", mode);
			break;
	}

	if(wasOn)
		SetLoadActive(channel, true);
}

void SiglentLoad::SetLoadCurrentRange(size_t channel, size_t rangeIndex)
{
	auto ranges = GetLoadCurrentRanges(channel);
	int fullScaleRange = ranges[rangeIndex];

	//Cannot change range while load is enabled
	bool wasOn = GetLoadActive(channel);
	if(wasOn)
		SetLoadActive(channel, false);

	auto mode = GetLoadMode(channel);
	switch(mode)
	{
		case MODE_CONSTANT_CURRENT:
			m_transport->SendCommandQueued(string("SOUR:CURR:IRANG ") + to_string(fullScaleRange));
			break;

		case MODE_CONSTANT_VOLTAGE:
			m_transport->SendCommandQueued(string("SOUR:VOLT:IRANG ") + to_string(fullScaleRange));
			break;

		default:
			LogWarning("[SiglentLoad::SetLoadCurrentRange] Unknown mode %d\n", mode);
			break;
	}

	if(wasOn)
		SetLoadActive(channel, true);
}

bool SiglentLoad::GetLoadActive(size_t /*channel*/)
{
	auto reply = Trim(m_transport->SendCommandQueuedWithReply("SOUR:INP:STAT?"));
	return stoi(reply) == 1;
}

void SiglentLoad::SetLoadActive(size_t /*channel*/, bool active)
{
	if(active)
		m_transport->SendCommandQueued("SOUR:INP:STAT 1");
	else
		m_transport->SendCommandQueued("SOUR:INP:STAT 0");
}

float SiglentLoad::GetLoadVoltageActual(size_t /*channel*/)
{
	return stof(Trim(m_transport->SendCommandQueuedWithReply("MEAS:VOLT?")));
}

float SiglentLoad::GetLoadCurrentActual(size_t /*channel*/)
{
	return stof(Trim(m_transport->SendCommandQueuedWithReply("MEAS:CURR?")));
}
