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
#include "Multimeter.h"

using namespace std;

Multimeter::Multimeter()
{
}

Multimeter::~Multimeter()
{
}

Unit Multimeter::GetMeterUnit()
{
	switch(GetMeterMode())
	{
		case Multimeter::FREQUENCY:
			return Unit(Unit::UNIT_HZ);

		case Multimeter::TEMPERATURE:
			return Unit(Unit::UNIT_CELSIUS);

		case Multimeter::DC_CURRENT:
		case Multimeter::AC_CURRENT:
			return Unit(Unit::UNIT_AMPS);

		case Multimeter::DC_VOLTAGE:
		case Multimeter::DC_RMS_AMPLITUDE:
		case Multimeter::AC_RMS_AMPLITUDE:
		default:
			return Unit(Unit::UNIT_VOLTS);
	}
}

Unit Multimeter::GetSecondaryMeterUnit()
{
	switch(GetSecondaryMeterMode())
	{
		case Multimeter::FREQUENCY:
			return Unit(Unit::UNIT_HZ);

		case Multimeter::TEMPERATURE:
			return Unit(Unit::UNIT_CELSIUS);

		case Multimeter::DC_CURRENT:
		case Multimeter::AC_CURRENT:
			return Unit(Unit::UNIT_AMPS);

		case Multimeter::DC_VOLTAGE:
		case Multimeter::DC_RMS_AMPLITUDE:
		case Multimeter::AC_RMS_AMPLITUDE:
		default:
			return Unit(Unit::UNIT_VOLTS);
	}
}

/**
	@brief Converts a meter mode to human readable text
 */
string Multimeter::ModeToText(MeasurementTypes type)
{
	switch(type)
	{
		case Multimeter::FREQUENCY:
			return "Frequency";
		case Multimeter::TEMPERATURE:
			return "Temperature";
		case Multimeter::DC_CURRENT:
			return "DC Current";
		case Multimeter::AC_CURRENT:
			return "AC Current";
		case Multimeter::DC_VOLTAGE:
			return "DC Voltage";
		case Multimeter::DC_RMS_AMPLITUDE:
			return "DC RMS Amplitude";
		case Multimeter::AC_RMS_AMPLITUDE:
			return "AC RMS Amplitude";

		default:
			return "";
	}
}

/**
	@brief Gets a bitmask of secondary measurement types currently available.

	The return value may change depending on the current primary measurement type.
 */
unsigned int Multimeter::GetSecondaryMeasurementTypes()
{
	//default to no secondary measurements
	return NONE;
}

/**
	@brief Gets the active secondary mode
 */
Multimeter::MeasurementTypes Multimeter::GetSecondaryMeterMode()
{
	//default to no measurement
	return NONE;
}

/**
	@brief Sets the active secondary mode
 */
void Multimeter::SetSecondaryMeterMode(MeasurementTypes /*type*/)
{
	//nothing to do
}

double Multimeter::GetSecondaryMeterValue()
{
	return 0;
}
