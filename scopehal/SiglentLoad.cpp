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

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SiglentLoad::SiglentLoad(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
{
	m_channels.push_back(new InstrumentChannel("LOAD", "#808080", Unit(Unit::UNIT_FS), 0));
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

Load::LoadMode SiglentLoad::GetLoadMode(size_t channel)
{
}

void SiglentLoad::SetLoadMode(size_t channel, LoadMode mode)
{
}

vector<float> SiglentLoad::GetLoadCurrentRanges(size_t channel)
{
}

size_t SiglentLoad::GetLoadCurrentRange(size_t channel)
{
}

vector<float> SiglentLoad::GetLoadVoltageRanges(size_t channel)
{
}

size_t SiglentLoad::GetLoadVoltageRange(size_t channel)
{
}
