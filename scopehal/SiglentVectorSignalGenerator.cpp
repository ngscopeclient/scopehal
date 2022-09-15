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
	return (stoi(m_transport->SendCommandQueuedWithReply("OUTP?")) == 1);
}

void SiglentVectorSignalGenerator::SetChannelOutputEnable(int /*chan*/, bool on)
{
	if(on)
		m_transport->SendCommandQueued("OUTP ON");
	else
		m_transport->SendCommandQueued("OUTP OFF");
}

float SiglentVectorSignalGenerator::GetChannelOutputPower(int /*chan*/)
{
	//FIXME: this does not return actual current value if sweeping
	//We will be able to use SWE:CURR:LEV in a future firmware (not yet released)

	return stof(m_transport->SendCommandQueuedWithReply("SOUR:POW?"));
}

void SiglentVectorSignalGenerator::SetChannelOutputPower(int /*chan*/, float power)
{
	m_transport->SendCommandQueued(string("SOUR:POW ") + to_string(power));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Synthesizer

float SiglentVectorSignalGenerator::GetChannelCenterFrequency(int /*chan*/)
{
	return stof(m_transport->SendCommandQueuedWithReply("SOUR:FREQ?"));
}

void SiglentVectorSignalGenerator::SetChannelCenterFrequency(int /*chan*/, float freq)
{
	//FIXME: this does not return actual current value if sweeping
	//We will be able to use SWE:CURR:FREQ in a future firmware (not yet released)

	m_transport->SendCommandQueued(string("SOUR:FREQ ") + to_string(freq));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Vector modulation

bool SiglentVectorSignalGenerator::IsVectorModulationAvailable(int /*chan*/)
{
	if(m_model.find("-V") != string::npos)
		return true;
	else
		return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sweeping

bool SiglentVectorSignalGenerator::IsSweepAvailable(int /*chan*/)
{
	return true;
}

float SiglentVectorSignalGenerator::GetSweepStartFrequency(int /*chan*/)
{
	return stof(m_transport->SendCommandQueuedWithReply("SOUR:SWE:STEP:STAR:FREQ?"));
}

float SiglentVectorSignalGenerator::GetSweepStopFrequency(int /*chan*/)
{
	return stof(m_transport->SendCommandQueuedWithReply("SOUR:SWE:STEP:STOP:FREQ?"));
}

void SiglentVectorSignalGenerator::SetSweepStartFrequency(int /*chan*/, float freq)
{
	m_transport->SendCommandQueued(string("SOUR:SWE:STEP:STAR:FREQ ") + to_string(freq));
}

void SiglentVectorSignalGenerator::SetSweepStopFrequency(int /*chan*/, float freq)
{
	m_transport->SendCommandQueued(string("SOUR:SWE:STEP:STOP:FREQ ") + to_string(freq));
}

float SiglentVectorSignalGenerator::GetSweepStartLevel(int /*chan*/)
{
	return stof(m_transport->SendCommandQueuedWithReply("SOUR:SWE:STEP:STAR:LEV?"));
}

float SiglentVectorSignalGenerator::GetSweepStopLevel(int /*chan*/)
{
	return stof(m_transport->SendCommandQueuedWithReply("SOUR:SWE:STEP:STOP:LEV?"));
}

void SiglentVectorSignalGenerator::SetSweepStartLevel(int /*chan*/, float level)
{
	m_transport->SendCommandQueued(string("SOUR:SWE:STEP:STAR:LEV ") + to_string(level));
}

void SiglentVectorSignalGenerator::SetSweepStopLevel(int /*chan*/, float level)
{
	m_transport->SendCommandQueued(string("SOUR:SWE:STEP:STOP:LEV ") + to_string(level));
}

void SiglentVectorSignalGenerator::SetSweepDwellTime(int /*chan*/, float fs)
{
	m_transport->SendCommandQueued(string("SOUR:SWE:STEP:DWEL ") + to_string(fs * SECONDS_PER_FS));
}

float SiglentVectorSignalGenerator::GetSweepDwellTime(int /*chan*/)
{
	return stof(m_transport->SendCommandQueuedWithReply("SOUR:SWE:STEP:DWEL?")) * FS_PER_SECOND;
}

void SiglentVectorSignalGenerator::SetSweepPoints(int /*chan*/, int npoints)
{
	m_transport->SendCommandQueued(string("SOUR:SWE:STEP:POIN ") + to_string(npoints));
}

int SiglentVectorSignalGenerator::GetSweepPoints(int /*chan*/)
{
	return stoi(m_transport->SendCommandQueuedWithReply("SOUR:SWE:STEP:POIN?"));
}

RFSignalGenerator::SweepShape SiglentVectorSignalGenerator::GetSweepShape(int /*chan*/)
{
	auto shape = m_transport->SendCommandQueuedWithReply("SOUR:SWE:STEP:SHAP?");
	if(shape.find("SAW") == 0)
		return SWEEP_SHAPE_SAWTOOTH;
	else
		return SWEEP_SHAPE_TRIANGLE;
}

void SiglentVectorSignalGenerator::SetSweepShape(int /*chan*/, SweepShape shape)
{
	switch(shape)
	{
		case SWEEP_SHAPE_SAWTOOTH:
			//Error in SSG5000X programming guide: short form of "sawtooth" is documented as "SAWtooth".
			//The actual value accepted by firmware is SAWTooth.
			m_transport->SendCommandQueued("SOUR:SWE:STEP:SHAP SAWT");
			break;

		case SWEEP_SHAPE_TRIANGLE:
			m_transport->SendCommandQueued("SOUR:SWE:STEP:SHAP TRI");
			break;

		default:
			break;
	}
}

RFSignalGenerator::SweepSpacing SiglentVectorSignalGenerator::GetSweepSpacing(int /*chan*/)
{
	auto shape = m_transport->SendCommandQueuedWithReply("SOUR:SWE:STEP:SPAC?");
	if(shape.find("LIN") == 0)
		return SWEEP_SPACING_LINEAR;
	else
		return SWEEP_SPACING_LOG;
}

void SiglentVectorSignalGenerator::SetSweepSpacing(int /*chan*/, SweepSpacing shape)
{
	switch(shape)
	{
		case SWEEP_SPACING_LINEAR:
			m_transport->SendCommandQueued("SOUR:SWE:STEP:SPAC LIN");
			break;

		case SWEEP_SPACING_LOG:
			m_transport->SendCommandQueued("SOUR:SWE:STEP:SPAC LOG");
			break;

		default:
			break;
	}
}

RFSignalGenerator::SweepDirection SiglentVectorSignalGenerator::GetSweepDirection(int /*chan*/)
{
	auto dir = m_transport->SendCommandQueuedWithReply("SOUR:SWE:DIR?");
	if(dir.find("FWD") == 0)
		return SWEEP_DIR_FWD;
	else
		return SWEEP_DIR_REV;
}

void SiglentVectorSignalGenerator::SetSweepDirection(int /*chan*/, SweepDirection dir)
{
	switch(dir)
	{
		case SWEEP_DIR_FWD:
			m_transport->SendCommandQueued("SOUR:SWE:DIR FWD");
			break;

		case SWEEP_DIR_REV:
			m_transport->SendCommandQueued("SOUR:SWE:DIR REV");
			break;

		default:
			break;
	}
}

RFSignalGenerator::SweepType SiglentVectorSignalGenerator::GetSweepType(int /*chan*/)
{
	auto shape = m_transport->SendCommandQueuedWithReply("SOUR:SWE:STAT?");
	if(shape.find("FREQ") == 0)
		return SWEEP_TYPE_FREQ;
	else if(shape.find("LEV_FREQ") == 0)
		return SWEEP_TYPE_FREQ_LEVEL;
	else if(shape.find("LEV") == 0)
		return SWEEP_TYPE_LEVEL;
	else
		return SWEEP_TYPE_NONE;
}

void SiglentVectorSignalGenerator::SetSweepType(int /*chan*/, SweepType type)
{
	switch(type)
	{
		case SWEEP_TYPE_NONE:
			m_transport->SendCommandQueued("SOUR:SWE:STAT OFF");
			break;

		case SWEEP_TYPE_FREQ:
			m_transport->SendCommandQueued("SOUR:SWE:STAT FREQ");
			break;

		case SWEEP_TYPE_LEVEL:
			m_transport->SendCommandQueued("SOUR:SWE:STAT LEV");
			break;

		case SWEEP_TYPE_FREQ_LEVEL:
			m_transport->SendCommandQueued("SOUR:SWE:STAT LEV_FREQ");
			break;

		default:
			break;
	}
}
