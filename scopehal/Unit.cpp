/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2021 Andrew D. Zonenberg and contributors                                                         *
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

using namespace std;

#ifdef _WIN32
string Unit::m_slocale;
#else
locale_t Unit::m_locale;
locale_t Unit::m_defaultLocale;
#endif

/**
	@brief Constructs a new unit from a string
 */
Unit::Unit(const string& rhs)
{
	if(rhs == "fs")
		m_type = UNIT_FS;
	else if(rhs == "Hz")
		m_type = UNIT_HZ;
	else if(rhs == "V")
		m_type = UNIT_VOLTS;
	else if(rhs == "A")
		m_type = UNIT_AMPS;
	else if(rhs == "Ω")
		m_type = UNIT_OHMS;
	else if(rhs == "b/s")
		m_type = UNIT_BITRATE;
	else if(rhs == "%")
		m_type = UNIT_PERCENT;
	else if(rhs == "dB")
		m_type = UNIT_DB;
	else if(rhs == "dBm")
		m_type = UNIT_DBM;
	else if(rhs == "unitless (linear)")
		m_type = UNIT_COUNTS;
	else if(rhs == "unitless (log)")
		m_type = UNIT_COUNTS_SCI;
	else if(rhs == "log BER")
		m_type = UNIT_LOG_BER;
	else if(rhs == "sa/s")
		m_type = UNIT_SAMPLERATE;
	else if(rhs == "sa")
		m_type = UNIT_SAMPLEDEPTH;
	else if(rhs == "W")
		m_type = UNIT_WATTS;
	else if(rhs == "UI")
		m_type = UNIT_UI;
	else if(rhs == "°")
		m_type = UNIT_DEGREES;
	else if(rhs == "RPM")
		m_type = UNIT_RPM;
	else if(rhs == "°C")
		m_type = UNIT_CELSIUS;
	else if(rhs == "ρ")
		m_type = UNIT_RHO;
	else if(rhs == "mV")
		m_type = UNIT_MILLIVOLTS;
	else
		LogWarning("Unrecognized unit \"%s\"\n", rhs.c_str());
}

/**
	@brief Converts this unit to a string
 */
string Unit::ToString()
{
	switch(m_type)
	{
		case UNIT_FS:
			return "fs";

		case UNIT_HZ:
			return "Hz";

		case UNIT_VOLTS:
			return "V";

		case UNIT_AMPS:
			return "A";

		case UNIT_OHMS:
			return "Ω";

		case UNIT_BITRATE:
			return "b/s";

		case UNIT_PERCENT:
			return "%";

		case UNIT_DB:
			return "dB";

		case UNIT_DBM:
			return "dBm";

		case UNIT_COUNTS:
			return "unitless (linear)";

		case UNIT_COUNTS_SCI:
			return "unitless (log)";

		case UNIT_LOG_BER:
			return "log BER";

		case UNIT_SAMPLERATE:
			return "sa/s";

		case UNIT_SAMPLEDEPTH:
			return "sa";

		case UNIT_WATTS:
			return "W";

		case UNIT_UI:
			return "UI";

		case UNIT_DEGREES:
			return "°";

		case UNIT_RPM:
			return "RPM";

		case UNIT_CELSIUS:
			return "°C";

		case UNIT_RHO:
			return "ρ";

		case UNIT_MILLIVOLTS:
			return "mV";

		default:
			return "unknown";
	}
}

/**
	@brief Prints a value with SI scaling factors

	@param value	The value
	@param digits	Number of significant digits to display
 */
string Unit::PrettyPrint(double value, int sigfigs)
{
	SetPrintingLocale();

	const char* scale = "";
	const char* unit = "";

	double value_rescaled = value;

	//Default scaling and prefixes for SI base units
	if(fabs(value) >= 1e9f)
	{
		value_rescaled /= 1e9;
		scale = "G";
	}
	else if(fabs(value) >= 1e6)
	{
		value_rescaled /= 1e6;
		scale = "M";
	}
	else if(fabs(value) >= 1e3)
	{
		value_rescaled /= 1e3;
		scale = "k";
	}
	else if(fabs(value) < 1)
	{
		value_rescaled *= 1e3;
		scale = "m";
	}
	else if(fabs(value) < 1e-6)
	{
		value_rescaled *= 1e6;
		scale = "μ";
	}
	else if(fabs(value) < 1e-9)
	{
		value_rescaled *= 1e9;
		scale = "n";
	}
	else if(fabs(value) < 1e-12)
	{
		value_rescaled *= 1e12;
		scale = "p";
	}

	bool space_after_number = true;
	switch(m_type)
	{
		//Special handling needed since it's not a SI base unit
		case UNIT_FS:
			unit = "s";

			if(fabs(value) >= 1e15)
			{
				value_rescaled = value / 1e15;
				scale = "";
			}
			else if(fabs(value) >= 1e12)
			{
				value_rescaled = value / 1e12;
				scale = "m";
			}
			else if(fabs(value) >= 1e9)
			{
				value_rescaled = value / 1e9;
				scale = "μ";
			}
			else if(fabs(value) >= 1e6)
			{
				value_rescaled = value / 1e6;
				scale = "n";
			}
			else if(fabs(value) >= 1e3)
			{
				value_rescaled = value / 1e3;
				scale = "p";
			}
			else
			{
				value_rescaled = value;
				scale = "f";
			}
			break;

		case UNIT_HZ:
			unit = "Hz";
			break;

		case UNIT_SAMPLERATE:
			unit = "S/s";
			break;

		case UNIT_SAMPLEDEPTH:
			unit = "S";
			break;

		case UNIT_VOLTS:
			unit = "V";
			break;

		//No scaling applied, forced to mV
		case UNIT_MILLIVOLTS:
			unit = "mV";
			value_rescaled = value;
			scale = "";
			break;

		case UNIT_AMPS:
			unit = "A";
			break;

		case UNIT_OHMS:
			unit = "Ω";
			break;

		case UNIT_WATTS:
			unit = "W";
			break;

		case UNIT_RHO:
			unit = "ρ";
			break;

		case UNIT_BITRATE:
			unit = "bps";
			break;
		case UNIT_UI:
			unit = " UI";	//move the space next to the number
			space_after_number = false;
			break;
		case UNIT_RPM:
			unit = "RPM";
			break;

		//Angular degrees do not use SI prefixes
		case UNIT_DEGREES:
			unit = "°";
			scale = "";
			value_rescaled = value;
			break;

		//Neither do thermal degrees
		case UNIT_CELSIUS:
			unit = "°C";
			scale = "";
			value_rescaled = value;
			break;

		//dBm are always reported as is, with no SI prefixes
		case UNIT_DBM:
			unit = "dBm";
			scale = "";
			value_rescaled = value;
			break;

		//Convert fractional value to percentage
		case UNIT_PERCENT:
			unit = "%";
			scale = "";
			value_rescaled = value * 100;
			break;

		case UNIT_COUNTS_SCI:
			unit = "#";
			break;

		//Dimensionless unit, no scaling applied
		case UNIT_DB:
			unit = "dB";
			scale = "";
			value_rescaled = value;
			break;
		case UNIT_COUNTS:
			unit = "";
			scale = "";
			value_rescaled = value;	//TODO: scientific notation flag?
			break;
		case UNIT_LOG_BER:
			unit = "";
			scale = "";
			value_rescaled = value;
			break;

		default:
			return "Invalid unit";
	}

	char tmp[128];
	switch(m_type)
	{
		case UNIT_LOG_BER:		//special formatting for BER since it's already logarithmic
			snprintf(tmp, sizeof(tmp), "1e%.0f", value);
			break;

		default:
			{
				const char* space = " ";
				if(!space_after_number)
					space = "";

				if(sigfigs > 0)
				{
					int leftdigits = 0;
					if(fabs(value_rescaled) > 1000)			//shouldn't have more than 4 digits w/ SI scaling
						leftdigits = 4;
					else if(fabs(value_rescaled) > 100)
						leftdigits = 3;
					else if(fabs(value_rescaled) > 10)
						leftdigits = 2;
					else if(fabs(value_rescaled) > 1)
						leftdigits = 1;
					int rightdigits = sigfigs - leftdigits;

					char format[32];
					snprintf(format, sizeof(format), "%%%d.%df%%s%%s%%s", leftdigits, rightdigits);
					snprintf(tmp, sizeof(tmp), format, value_rescaled, space, scale, unit);
				}

				//If not a round number, add more digits (up to 4)
				else
				{
					if( fabs(round(value_rescaled) - value_rescaled) < 0.001 )
						snprintf(tmp, sizeof(tmp), "%.0f%s%s%s", value_rescaled, space, scale, unit);
					else if(fabs(round(value_rescaled*10) - value_rescaled*10) < 0.001)
						snprintf(tmp, sizeof(tmp), "%.1f%s%s%s", value_rescaled, space, scale, unit);
					else if(fabs(round(value_rescaled*100) - value_rescaled*100) < 0.001 )
						snprintf(tmp, sizeof(tmp), "%.2f%s%s%s", value_rescaled, space, scale, unit);
					else if(fabs(round(value_rescaled*1000) - value_rescaled*1000) < 0.001 )
						snprintf(tmp, sizeof(tmp), "%.3f%s%s%s", value_rescaled, space, scale, unit);
					else
						snprintf(tmp, sizeof(tmp), "%.4f%s%s%s", value_rescaled, space, scale, unit);
				}
			}
			break;
	}

	SetDefaultLocale();
	return string(tmp);
}

/**
	@brief Parses a string based on the supplied unit
 */
double Unit::ParseString(const string& str)
{
	SetPrintingLocale();

	//Find the first non-numeric character in the strnig
	double scale = 1;
	for(size_t i=0; i<str.size(); i++)
	{
		char c = str[i];
		if(isspace(c) || isdigit(c) || (c == '.') || (c == '-') )
			continue;

		if(c == 'G')
			scale = 1000000000.0;
		else if(c == 'M')
			scale = 1000000.0;
		else if(c == 'K' || c == 'k')
			scale = 1000.0;
		else if(c == 'm')
			scale = 0.001;
		else if( (c == 'u') || (str.find("μ", i) == i) )
			scale = 1e-6;
		else if(c == 'n')
			scale = 1e-9;
		else if(c == 'p')
			scale = 1e-12;
		else if(c == 'f')
			scale = 1e-15;

		break;
	}

	//Parse the base value
	double ret;
	sscanf(str.c_str(), "%20lf", &ret);
	ret *= scale;

	//Apply a unit-specific scaling factor
	switch(m_type)
	{
		case Unit::UNIT_FS:
			ret *= 1e15;
			break;

		case Unit::UNIT_PERCENT:
			ret *= 0.01;
			break;

		default:
			break;
	}

	SetDefaultLocale();
	return ret;
}

/**
	@brief Multiplies two units and calculates the resulting unit
 */
Unit Unit::operator*(const Unit& rhs)
{
	//Voltage times current is power
	if( ( (m_type == UNIT_VOLTS) && (rhs.m_type == UNIT_AMPS) ) ||
		( (rhs.m_type == UNIT_VOLTS) && (m_type == UNIT_AMPS) ) )
	{
		return Unit(UNIT_WATTS);
	}

	//Unknown / invalid pairing?
	//For now, just return the first unit.
	//TODO: how should we handle this
	return Unit(m_type);
}

void Unit::SetLocale(const char* locale)
{
#ifdef _WIN32
	m_slocale = locale;
#else
	m_locale = newlocale(LC_ALL, locale, 0);

	m_defaultLocale = newlocale(LC_ALL, "C", 0);
#endif
}

/**
	@brief Sets the current locale to the user's selected LC_NUMERIC for printing numbers for display
 */
void Unit::SetPrintingLocale()
{
	#ifdef _WIN32
		setlocale(LC_NUMERIC, m_slocale.c_str());
	#else
		uselocale(m_locale);
	#endif
}

/**
	@brief Sets the current locale to "C" for interchange
 */
void Unit::SetDefaultLocale()
{
	#ifdef _WIN32
		setlocale(LC_NUMERIC, "C");
	#else
		uselocale(m_defaultLocale);
	#endif
}
