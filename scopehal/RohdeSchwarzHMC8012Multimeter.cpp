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

#include "scopehal.h"
#include "RohdeSchwarzHMC8012Multimeter.h"
#include "MultimeterChannel.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

RohdeSchwarzHMC8012Multimeter::RohdeSchwarzHMC8012Multimeter(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, m_modeValid(false)
	, m_secmodeValid(false)
	, m_dmmAutorangeValid(false)
{
	//prefetch operating mode
	GetMeterMode();

	//Create our single channel
	m_channels.push_back(new MultimeterChannel(this, "VIN", "#808080", 0));
}

RohdeSchwarzHMC8012Multimeter::~RohdeSchwarzHMC8012Multimeter()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device info

string RohdeSchwarzHMC8012Multimeter::GetDriverNameInternal()
{
	return "rs_hmc8012";
}

unsigned int RohdeSchwarzHMC8012Multimeter::GetInstrumentTypes() const
{
	return INST_DMM;
}

unsigned int RohdeSchwarzHMC8012Multimeter::GetMeasurementTypes()
{
	return AC_RMS_AMPLITUDE | DC_VOLTAGE | DC_CURRENT | AC_CURRENT | TEMPERATURE;
}

unsigned int RohdeSchwarzHMC8012Multimeter::GetSecondaryMeasurementTypes()
{
	switch(m_mode)
	{
		case AC_RMS_AMPLITUDE:
		case AC_CURRENT:
			return FREQUENCY;

		default:
			return 0;
	}
}

uint32_t RohdeSchwarzHMC8012Multimeter::GetInstrumentTypesForChannel(size_t /*i*/) const
{
	return INST_DMM;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DMM mode

int RohdeSchwarzHMC8012Multimeter::GetMeterDigits()
{
	return 6;
}

bool RohdeSchwarzHMC8012Multimeter::GetMeterAutoRange()
{
	if(m_dmmAutorangeValid)
		return m_dmmAutorange;

	string reply;

	switch(m_mode)
	{
		case AC_RMS_AMPLITUDE:
			reply = m_transport->SendCommandQueuedWithReply("SENSE:VOLT:AC:RANGE:AUTO?");
			break;

		case DC_VOLTAGE:
			reply = m_transport->SendCommandQueuedWithReply("SENSE:VOLT:DC:RANGE:AUTO?");
			break;

		case AC_CURRENT:
			reply = m_transport->SendCommandQueuedWithReply("SENSE:CURR:AC:RANGE:AUTO?");
			break;

		case DC_CURRENT:
			reply = m_transport->SendCommandQueuedWithReply("SENSE:CURR:DC:RANGE:AUTO?");
			break;

		//no autoranging in temperature mode
		case TEMPERATURE:
			m_dmmAutorangeValid = true;
			m_dmmAutorange = false;
			return false;

		//TODO
		default:
			LogError("GetMeterAutoRange not implemented yet for modes other than DC_CURRENT\n");
			m_dmmAutorangeValid = true;
			m_dmmAutorange = false;
			return false;
	}

	m_dmmAutorange = (reply == "1");
	m_dmmAutorangeValid = true;
	return m_dmmAutorange;
}

void RohdeSchwarzHMC8012Multimeter::SetMeterAutoRange(bool enable)
{
	m_dmmAutorange = enable;
	m_dmmAutorangeValid = true;

	switch(m_mode)
	{
		case AC_RMS_AMPLITUDE:
			m_transport->SendCommandQueued(string("SENSE:VOLT:AC:RANGE:AUTO ") + to_string(enable));
			break;

		case DC_VOLTAGE:
			m_transport->SendCommandQueued(string("SENSE:VOLT:DC:RANGE:AUTO ") + to_string(enable));
			break;

		case AC_CURRENT:
			m_transport->SendCommandQueued(string("SENSE:CURR:AC:RANGE:AUTO ") + to_string(enable));
			break;

		case DC_CURRENT:
			m_transport->SendCommandQueued(string("SENSE:CURR:DC:RANGE:AUTO ") + to_string(enable));
			break;

		//no autoranging in temperature mode
		case TEMPERATURE:
			return;

		default:
			LogError("SetMeterAutoRange not implemented yet for modes other than DC_CURRENT\n");
	}
}

void RohdeSchwarzHMC8012Multimeter::StartMeter()
{
	//cannot be started or stopped
}

void RohdeSchwarzHMC8012Multimeter::StopMeter()
{
	//cannot be started or stopped
}

double RohdeSchwarzHMC8012Multimeter::GetMeterValue()
{
	return stod(m_transport->SendCommandQueuedWithReply("FETCH?"));
}

double RohdeSchwarzHMC8012Multimeter::GetSecondaryMeterValue()
{
	//If we have a secondary value, this gets it
	//If no secondary mode configured, returns primary value
	return stod(m_transport->SendCommandQueuedWithReply("READ?"));
}

int RohdeSchwarzHMC8012Multimeter::GetCurrentMeterChannel()
{
	return 0;
}

void RohdeSchwarzHMC8012Multimeter::SetCurrentMeterChannel(int /*chan*/)
{
	//nop
}

Multimeter::MeasurementTypes RohdeSchwarzHMC8012Multimeter::GetMeterMode()
{
	if(m_modeValid)
		return m_mode;

	auto str = m_transport->SendCommandQueuedWithReply("CONF?");

	char mode[32];
	sscanf(str.c_str(), "\"%31[^,]", mode);
	string smode = mode;

	//Default to no alternate mode
	m_secmode = NONE;

	if(smode == "CURR")
		m_mode = DC_CURRENT;
	else if(smode == "CURR:AC")
		m_mode = AC_CURRENT;
	else if(smode == "SENS")
		m_mode = TEMPERATURE;
	else if(smode == "VOLT")
		m_mode = DC_VOLTAGE;
	else if(smode == "VOLT:AC")
		m_mode = AC_RMS_AMPLITUDE;
	else if(smode == "FREQ:VOLT")
	{
		m_mode = AC_RMS_AMPLITUDE;
		m_secmode = FREQUENCY;
	}

	//unknown, pick something
	else
	{
		LogDebug("smode = %s\n", smode.c_str());
		m_mode = DC_VOLTAGE;
	}

	m_modeValid = true;
	m_secmodeValid = true;
	return m_mode;
}

Multimeter::MeasurementTypes RohdeSchwarzHMC8012Multimeter::GetSecondaryMeterMode()
{
	if(m_secmodeValid)
		return m_secmode;

	GetMeterMode();
	return m_secmode;
}

void RohdeSchwarzHMC8012Multimeter::SetMeterMode(Multimeter::MeasurementTypes type)
{
	m_secmode = NONE;

	switch(type)
	{
		case AC_RMS_AMPLITUDE:
			m_transport->SendCommandQueued("CONF:VOLT:AC");
			break;

		case DC_VOLTAGE:
			m_transport->SendCommandQueued("CONF:VOLT:DC");
			break;

		case DC_CURRENT:
			m_transport->SendCommandQueued("CONF:CURR:DC");
			break;

		case AC_CURRENT:
			m_transport->SendCommandQueued("CONF:CURR:DC");
			break;

		case TEMPERATURE:	//TODO: type of temp sensor
			m_transport->SendCommandQueued("CONF:TEMP");
			break;

		//whatever it is, not supported
		default:
			return;
	}

	m_mode = type;
}

void RohdeSchwarzHMC8012Multimeter::SetSecondaryMeterMode(Multimeter::MeasurementTypes type)
{
	auto mode = GetMeterMode();

	switch(type)
	{
		case FREQUENCY:
			{
				switch(mode)
				{
					case AC_RMS_AMPLITUDE:
						m_transport->SendCommandQueued("CONF:FREQ:VOLT");
						break;

					case AC_CURRENT:
						m_transport->SendCommandQueued("CONF:FREQ:CURR");
						break;

					//not supported
					default:
						return;
				}
			}
			break;

		case NONE:
			SetMeterMode(mode);
			break;

		//not supported
		default:
			return;
	}

	m_secmode = type;
	m_secmodeValid = true;
}
