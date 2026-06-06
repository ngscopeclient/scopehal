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
	@author Tim Pattinson
	@brief Agilent 34401A multimeter driver
 */
#include <string>

#include "scopehal.h"
#include "AgilentMultimeter.h"
#include "MultimeterChannel.h"

using namespace std;

// Implemented
// Basic measurements


// Not implemented
// Triggering
// Sampling multiple values
// Period, 4W ohms
// Configuring digits or NPLC
// Configuring bandwidth
// Configuring input impedance

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

AgilentMultimeter::AgilentMultimeter(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
	, m_modeValid(false)
{

	//Create our single channel
	m_channels.push_back(new MultimeterChannel(this, "VIN", "#ffff00", 0));

	// Set *long* timeouts required for certain measurements
	m_transport->SetTimeouts((unsigned int)1e6*0.5,(unsigned int)1e6*2);

	// Need to be in remote mode to do anything useful
	m_transport->SendCommand("SYST:REM");

	// Reset
	//m_transport->SendCommandQueuedWithReply("*RST");
	
	// Clear errors
	m_transport->SendCommandQueuedWithReply("*CLS");

	//prefetch operating mode
	GetMeterMode();

}

AgilentMultimeter::~AgilentMultimeter()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device info

string AgilentMultimeter::GetDriverNameInternal()
{
	return "agilent_dmm";
}

unsigned int AgilentMultimeter::GetInstrumentTypes() const
{
	return INST_DMM;
}

unsigned int AgilentMultimeter::GetMeasurementTypes()
{
	return AC_RMS_AMPLITUDE | DC_VOLTAGE | DC_CURRENT | AC_CURRENT | RESISTANCE | CONTINUITY | DIODE | FREQUENCY;
	// TODO: PERIOD when supported by Multimeter class
	// TODO: OHMS 4W
	// TODO: DCV RATIO when supported
}

unsigned int AgilentMultimeter::GetSecondaryMeasurementTypes()
{
	// 34401A has no secondary measurement capability
    return 0;
}

uint32_t AgilentMultimeter::GetInstrumentTypesForChannel([[maybe_unused]] size_t i) const
{
	return INST_DMM;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DMM mode

int AgilentMultimeter::GetMeterDigits()
{
	return 7;
}


bool AgilentMultimeter::GetMeterAutoRange()
{

	string s_autoRangeReply;
	auto mode = GetMeterMode(); // should be cached

	switch(mode)
	{
		case AC_RMS_AMPLITUDE:
			s_autoRangeReply = m_transport->SendCommandQueuedWithReply("SENS:VOLT:DC:RANG:AUTO?");
			break;
		case DC_VOLTAGE:
			s_autoRangeReply = m_transport->SendCommandQueuedWithReply("SENS:VOLT:DC:RANG:AUTO?");
			break;
		case DC_CURRENT:
			s_autoRangeReply = m_transport->SendCommandQueuedWithReply("SENS:CURR:DC:RANG:AUTO?");
			break;
		case AC_CURRENT:
			s_autoRangeReply = m_transport->SendCommandQueuedWithReply("SENS:CURR:AC:RANG:AUTO?");
			break;
		case RESISTANCE:
			s_autoRangeReply = m_transport->SendCommandQueuedWithReply("SENS:RES:RANG:AUTO?");
			break;
		case CONTINUITY:
			// no such thing as ranging
			return false;
		case DIODE:
			// no such thing as ranging
			return false;
		case FREQUENCY:
			s_autoRangeReply = m_transport->SendCommandQueuedWithReply("SENS:FREQ:VOLT:RANG:AUTO?");
			// Ranging applies to the AC voltage measured
			break;
		default:
			LogError("Unknown meter mode \n");
	}
	
	if(Trim(s_autoRangeReply) == "1"){
		return true;
	}else if(Trim(s_autoRangeReply) == "0"){
		return false;
	}else{
		LogError("Unexpected reply for auto range %s\n",s_autoRangeReply.c_str());
		return false;
	}


}

void AgilentMultimeter::SetMeterAutoRange(bool enable)
{
	string s_AutoRange = enable? "ON" : "OFF";

	auto mode = GetMeterMode(); // should be cached

	switch(mode)
	{
		case AC_RMS_AMPLITUDE:
			m_transport->SendCommandQueuedWithReply("SENS:VOLT:AC:RANG:AUTO "+s_AutoRange+";*OPC?");
			break;
		case DC_VOLTAGE:
			m_transport->SendCommandQueuedWithReply("SENS:VOLT:DC:RANG:AUTO "+s_AutoRange+";*OPC?");
			break;
		case DC_CURRENT:
			m_transport->SendCommandQueuedWithReply("SENS:CURR:DC:RANG:AUTO "+s_AutoRange+";*OPC?");
			break;
		case AC_CURRENT:
			m_transport->SendCommandQueuedWithReply("SENS:CURR:AC:RANG:AUTO "+s_AutoRange+";*OPC?");
			break;
		case RESISTANCE:
			m_transport->SendCommandQueuedWithReply("SENS:RES:RANG:AUTO "+s_AutoRange+";*OPC?");
			break;
		case CONTINUITY:
			// no such thing as ranging
			if(enable)
				LogError("Auto-range not supported in Continuity mode\n");
			break;
		case DIODE:
			// no such thing as ranging
			if(enable)
				LogError("Auto-range not supported in Diode mode\n");
			break;
		case FREQUENCY:
			m_transport->SendCommandQueuedWithReply("SENS:FREQ:VOLT:RANG:AUTO "+s_AutoRange+";*OPC?");
			// Ranging applies to the AC voltage measured
			break;
		default:
			LogError("Unknown meter mode \n");
			break;
	}
	

}

void AgilentMultimeter::StartMeter()
{
	//cannot be started or stopped
}

void AgilentMultimeter::StopMeter()
{
	//cannot be started or stopped
}

double AgilentMultimeter::GetMeterValue()
{
	string value;
	while(true)
	{
		LogTrace("sent READ?\n");
		value = Trim(m_transport->SendCommandQueuedWithReply("READ?"));
		LogTrace("reply for READ? %s\n",value.c_str());
		
		if(value.empty())
		{
			LogWarning("Failed to read value: got '%s'\n",value.c_str());
			continue;
		}
		else if(value == "+9.90000000E+37") // Overload
			return std::numeric_limits<double>::max();

		istringstream os(value);
    	double result;
    	os >> result;
		return result;
	}
}

double AgilentMultimeter::GetSecondaryMeterValue()
{
	// 34401A has no secondary measurement capability
    return 0.0;
}

int AgilentMultimeter::GetCurrentMeterChannel()
{
	return 0;
}

void AgilentMultimeter::SetCurrentMeterChannel([[maybe_unused] ]int chan)
{
	//nop
}

Multimeter::MeasurementTypes AgilentMultimeter::GetMeterMode()
{
	if(m_modeValid){
		return m_mode;
	}

	LogTrace("sent FUNC?\n");
	auto s_modeReply = TrimQuotes(Trim(m_transport->SendCommandQueuedWithReply("FUNC?")));
	LogTrace("reply for FUNC? %s\n",s_modeReply.c_str());

	// Split with the space to get a string with the mode
	if(!s_modeReply.empty()){
		if(s_modeReply == "VOLT:AC")
			m_mode = AC_RMS_AMPLITUDE;
		else if(s_modeReply == "VOLT")
			m_mode = DC_VOLTAGE;
		else if(s_modeReply == "CURR:AC")
			m_mode = AC_CURRENT;
		else if(s_modeReply == "CURR")
			m_mode = DC_CURRENT;
		else if(s_modeReply == "FREQ")
			m_mode = FREQUENCY;
		else if(s_modeReply == "CONT")
			m_mode = CONTINUITY;
		else if(s_modeReply == "DIOD")
			m_mode = DIODE;
		else if(s_modeReply == "RES")
			m_mode = RESISTANCE;
		//unknown, pick something
		else
		{
			LogWarning("Unknown mode = '%s'\n", s_modeReply.c_str());
			m_mode = NONE;
		}

	}else{
		// bad SCPI reply or no SCPI reply
		LogWarning("Bad SCPI reply getting mode = '%s'\n", s_modeReply.c_str());
		m_mode = NONE;
	}
	
	m_modeValid = true;
	return m_mode;

	
	
}

Multimeter::MeasurementTypes AgilentMultimeter::GetSecondaryMeterMode()
{
	// 34401A has no secondary measurement capability
    return Multimeter::MeasurementTypes::NONE;
}

void AgilentMultimeter::SetMeterMode(Multimeter::MeasurementTypes type)
{
	string s_Reply;
	switch(type)
	{
		case DC_VOLTAGE:
			//m_transport->SendCommandImmediate("CONF:VOLT:DC");
			//s_Reply = m_transport->SendCommandQueuedWithReply("CONF:VOLT:DC;*OPC?");
			//LogTrace("OPC reply: %s",s_Reply.c_str());
			
			m_transport->SendCommandQueuedWithReply("CONF:VOLT:DC;*OPC?");
			// Sending *OPC? afterwards lets this block the SCPI bus so the meter can do its setup, rather than sending the next
			// command right away.
			break;

		case AC_RMS_AMPLITUDE:
			m_transport->SendCommandQueuedWithReply("CONF:VOLT:AC;*OPC?");
			break;

		case DC_CURRENT:
			m_transport->SendCommandQueuedWithReply("CONF:CURR:DC;*OPC?");
			break;

		case AC_CURRENT:
			m_transport->SendCommandQueuedWithReply("CONF:CURR:AC;*OPC?");
			break;

		case RESISTANCE:
			m_transport->SendCommandQueuedWithReply("CONF:RES;*OPC?");
			break;

		case FREQUENCY:
			m_transport->SendCommandQueuedWithReply("CONF:FREQ;*OPC?");
			break;

		case DIODE:
			m_transport->SendCommandQueuedWithReply("CONF:DIOD;*OPC?");
			break;

		case CONTINUITY:
			m_transport->SendCommandQueuedWithReply("CONF:CONT;*OPC?");
			break;

		//whatever it is, not supported
		default:
			LogWarning("Unexpected multimeter mode = '%d'", type);
			return;
	}
	//LogTrace("Probably set mode\n");
	m_mode = type;

}

void AgilentMultimeter::SetSecondaryMeterMode(Multimeter::MeasurementTypes type)
{
	// 34401A has no secondary measurement capability
	return;
}
