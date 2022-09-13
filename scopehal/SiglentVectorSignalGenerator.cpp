/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2022 Andrew D. Zonenberg and contributors                                                         *
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
#include "SiglentVectorSignalGenerator.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SiglentVectorSignalGenerator::SiglentVectorSignalGenerator(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
{
	//TODO: query options to figure out what we actually have
	//m_transport->SendCommand("*OPT?");
}

SiglentVectorSignalGenerator::~SiglentVectorSignalGenerator()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// System info / configuration

string SiglentVectorSignalGenerator::GetDriverNameInternal()
{
	return "siglent_ssg";
}

int SiglentVectorSignalGenerator::GetChannelCount()
{
	return 1;
}

string SiglentVectorSignalGenerator::GetChannelName(int /*chan*/)
{
	return "RFOUT";
}

unsigned int SiglentVectorSignalGenerator::GetInstrumentTypes()
{
	return INST_RF_GEN;
}

string SiglentVectorSignalGenerator::GetName()
{
	return m_model;
}

string SiglentVectorSignalGenerator::GetVendor()
{
	return m_vendor;
}

string SiglentVectorSignalGenerator::GetSerial()
{
	return m_serial;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Output stage

bool SiglentVectorSignalGenerator::GetChannelOutputEnable(int /*chan*/)
{
	m_transport->SendCommand("OUTP?");
	return (stoi(m_transport->ReadReply()) == 1);
}

void SiglentVectorSignalGenerator::SetChannelOutputEnable(int /*chan*/, bool on)
{
	if(on)
		m_transport->SendCommand("OUTP ON");
	else
		m_transport->SendCommand("OUTP OFF");
}

float SiglentVectorSignalGenerator::GetChannelOutputPower(int /*chan*/)
{
	m_transport->SendCommand("SOUR:POW?");
	return stof(m_transport->ReadReply());
}

void SiglentVectorSignalGenerator::SetChannelOutputPower(int /*chan*/, float power)
{
	m_transport->SendCommand(string("SOUR:POW ") + to_string(power));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Synthesizer

float SiglentVectorSignalGenerator::GetChannelCenterFrequency(int /*chan*/)
{
	m_transport->SendCommand("SOUR:FREQ?");
	return stof(m_transport->ReadReply());
}

void SiglentVectorSignalGenerator::SetChannelCenterFrequency(int /*chan*/, float freq)
{
	m_transport->SendCommand(string("SOUR:FREQ ") + to_string(freq));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Vector modulation

bool SiglentVectorSignalGenerator::IsVectorModulationAvailable(int chan)
{
	//TODO
	return true;
}
