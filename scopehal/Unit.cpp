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
string Unit::ToString() const
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
	@brief Gets the appropriate SI scaling factor for a number.
 */
void Unit::GetSIScalingFactor(double num, double& scaleFactor, string& prefix) const
{
	scaleFactor = 1;
	prefix = "";
	num = fabs(num);

	if(num >= 1e12f)
	{
		scaleFactor = 1e-12;
		prefix = "T";
	}
	else if(num >= 1e9f)
	{
		scaleFactor = 1e-9;
		prefix = "G";
	}
	else if(num >= 1e6)
	{
		scaleFactor = 1e-6;
		prefix = "M";
	}
	else if(num >= 1e3)
	{
		scaleFactor = 1e-3;
		prefix = "k";
	}
	else if(num < 1)
	{
		scaleFactor = 1e3;
		prefix = "m";
	}
	else if(num < 1e-6)
	{
		scaleFactor = 1e6;
		prefix = "μ";
	}
	else if(num < 1e-9)
	{
		scaleFactor = 1e9;
		prefix = "n";
	}
	else if(num < 1e-12)
	{
		scaleFactor = 1e12;
		prefix = "p";
	}
	else if(num < 1e-15)
	{
		scaleFactor = 1e15;
		prefix = "f";
	}
}

/**
	@brief Gets the suffix for a unit.

	Note that this function may modify the SI scale factor and prefix
 */
void Unit::GetUnitSuffix(UnitType type, double num, double& scaleFactor, string& prefix, string& suffix) const
{
	switch(type)
	{
		//Special handling needed around prefixes, since it's not a SI base unit
		case UNIT_FS:
			suffix = "s";

			if(fabs(num) >= 1e15)
			{
				scaleFactor = 1e-15;
				prefix = "";
			}
			else if(fabs(num) >= 1e12)
			{
				scaleFactor = 1e-12;
				prefix = "m";
			}
			else if(fabs(num) >= 1e9)
			{
				scaleFactor = 1e-9;
				prefix = "μ";
			}
			else if(fabs(num) >= 1e6)
			{
				scaleFactor = 1e-6;
				prefix = "n";
			}
			else if(fabs(num) >= 1e3)
			{
				scaleFactor = 1e-3;
				prefix = "p";
			}
			else
			{
				scaleFactor = 1;
				prefix = "f";
			}
			break;

		case UNIT_HZ:
			suffix = "Hz";
			break;

		case UNIT_SAMPLERATE:
			suffix = "S/s";
			break;

		case UNIT_SAMPLEDEPTH:
			suffix = "S";
			break;

		case UNIT_VOLTS:
			suffix = "V";
			break;

		//No scaling applied, forced to mV (special case)
		case UNIT_MILLIVOLTS:
			suffix = "mV";
			scaleFactor = 1;
			prefix = "";
			break;

		case UNIT_AMPS:
			suffix = "A";
			break;

		case UNIT_OHMS:
			suffix = "Ω";
			break;

		case UNIT_WATTS:
			suffix = "W";
			break;

		case UNIT_RHO:
			suffix = "ρ";
			break;

		case UNIT_BITRATE:
			suffix = "bps";
			break;
		case UNIT_UI:
			suffix = " UI";	//move the space next to the number
			break;
		case UNIT_RPM:
			suffix = "RPM";
			break;

		//Angular degrees do not use SI prefixes
		case UNIT_DEGREES:
			suffix = "°";
			prefix = "";
			scaleFactor = 1;
			break;

		//Neither do thermal degrees
		case UNIT_CELSIUS:
			suffix = "°C";
			prefix = "";
			scaleFactor = 1;
			break;

		//dBm are always reported as is, with no SI prefixes
		case UNIT_DBM:
			suffix = "dBm";
			prefix = "";
			scaleFactor = 1;
			break;

		//Convert fractional num to percentage
		case UNIT_PERCENT:
			suffix = "%";
			prefix = "";
			scaleFactor = 100;
			break;

		case UNIT_COUNTS_SCI:
			suffix = "#";
			break;

		//Dimensionless unit, no scaling applied
		case UNIT_DB:
			suffix = "dB";
			prefix = "";
			scaleFactor = 1;
			break;
		case UNIT_COUNTS:
		case UNIT_LOG_BER:
			suffix = "";
			prefix = "";
			scaleFactor = 1;
			break;

		default:
			break;
	}
}


/**
	@brief Prints a value with SI scaling factors

	@param value				The value
	@param digits				Number of significant digits to display
	@param useDisplayLocale		True if the string is formatted for display (user's locale)
								False if the string is formatted for serialization ("C" locale regardless of user pref)
 */
string Unit::PrettyPrint(double value, int sigfigs, bool useDisplayLocale) const
{
	if(useDisplayLocale)
		SetPrintingLocale();

	//Figure out scaling, prefix, and suffix
	double scaleFactor;
	string prefix;
	string suffix;
	GetSIScalingFactor(value, scaleFactor, prefix);
	GetUnitSuffix(m_type, value, scaleFactor, prefix, suffix);

	double value_rescaled = value * scaleFactor;
	bool space_after_number = (m_type != Unit::UNIT_UI);

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

					string format = string("%") + to_string(leftdigits) + "." + to_string(rightdigits) + "f%s%s%s";
					snprintf(tmp, sizeof(tmp), format.c_str(), value_rescaled, space, prefix.c_str(), suffix.c_str());
				}

				//If not a round number, add more digits (up to 4)
				else
				{
					if( fabs(round(value_rescaled) - value_rescaled) < 0.001 )
						snprintf(tmp, sizeof(tmp), "%.0f%s%s%s", value_rescaled, space, prefix.c_str(), suffix.c_str());
					else if(fabs(round(value_rescaled*10) - value_rescaled*10) < 0.001)
						snprintf(tmp, sizeof(tmp), "%.1f%s%s%s", value_rescaled, space, prefix.c_str(), suffix.c_str());
					else if(fabs(round(value_rescaled*100) - value_rescaled*100) < 0.001 )
						snprintf(tmp, sizeof(tmp), "%.2f%s%s%s", value_rescaled, space, prefix.c_str(), suffix.c_str());
					else if(fabs(round(value_rescaled*1000) - value_rescaled*1000) < 0.001 )
						snprintf(tmp, sizeof(tmp), "%.3f%s%s%s", value_rescaled, space, prefix.c_str(), suffix.c_str());
					else
						snprintf(tmp, sizeof(tmp), "%.4f%s%s%s", value_rescaled, space, prefix.c_str(), suffix.c_str());
				}
			}
			break;
	}

	SetDefaultLocale();
	return string(tmp);
}

/**
	@brief Prints a value with SI scaling factors and unnecessarily significant sub-pixel digits removed

	The rangeMin/rangeMax values are typically used to ensure all axis labels on a graph use consistent units,
	for example 0.5V / 1.0V / 1.5V rather than 500 mV / 1.0V / 1.5V.

	The pixelMin and pixelMax values are used to determine how many digits are actually significant. Rounding is in the
	upward direction.

	For example, if the pixel covers values 1.3979 to 1.4152, this function will return "1.4".

	@param pixelMin		Value at the lowest end of the pixel being labeled
	@param pixelMax		Value at the highest end of the pixel being labeled
	@param rangeMin		Value at the lowest end of the channel range
	@param rangeMax		Value at the highest end of the channel range
 */
string Unit::PrettyPrintRange(double pixelMin, double pixelMax, double rangeMin, double rangeMax) const
{
	SetPrintingLocale();

	//Figure out the scale factor to use. Use the full-scale range to select the factor even if we're small here
	double scaleFactor;
	string prefix;
	string suffix;
	double extremeValue = max(fabs(rangeMin), fabs(rangeMax));
	GetSIScalingFactor(extremeValue, scaleFactor, prefix);
	GetUnitSuffix(m_type, extremeValue, scaleFactor, prefix, suffix);

	//Swap values if they're reversed
	if(fabs(pixelMin) > fabs(pixelMax))
	{
		double tmp = pixelMax;
		pixelMax = pixelMin;
		pixelMin = tmp;
	}

	//Get the actual values to print
	double valueMinRescaled = pixelMin * scaleFactor;
	double valueMaxRescaled = pixelMax * scaleFactor;

	//Special case for log BER which is already logarithmic and doesn't need scaling
	const size_t buflen = 32;
	char tmp1[buflen];
	char tmp2[buflen];
	if(m_type == Unit::UNIT_LOG_BER)
	{
		snprintf(tmp1, sizeof(tmp1), "1e%.0f", valueMinRescaled);
		SetDefaultLocale();
		return string(tmp1);
	}

	//Do the actual float to ascii conversion
	snprintf(tmp1, sizeof(tmp1), "%.5f", valueMinRescaled);
	snprintf(tmp2, sizeof(tmp2), "%.5f", valueMaxRescaled);

	string out;

	//Special case: if zero is somewhere in the pixel, just print zero
	if( (valueMinRescaled <= 0) && (valueMaxRescaled >= 0) )
		out = "0";

	else
	{
		size_t i = 0;

		//Minus sign just gets echoed as-is
		//(we know both sides are negative if we get here, no need to check max value)
		if(valueMinRescaled < 0)
		{
			out += "-";
			i = 1;
		}

		//Pick out only the significant digits (tmp2 is always the larger magnitude)
		bool foundDecimal = false;
		for(; i<buflen; i++)
		{
			//If both digits are the same, echo to the output
			if(tmp1[i] == tmp2[i])
			{
				out += tmp1[i];
				if(!isdigit(tmp1[i]))
					foundDecimal = true;
			}

			//If either string ends, stop
			else if( (tmp1[i] == '\0') || (tmp2[i] == '\0') )
				break;

			//Mismatch! Figure out how to handle it
			else
			{
				//Mismatched significant digit after decimal (10.3, 10.4): just print the bigger digit and stop
				if(foundDecimal)
					out += tmp2[i];

				//Mismatched significant digit before decimal (125, 133): print bigger digit then zeroes
				else
				{
					out += tmp2[i];
					i++;

					//Pad with zeroes until we hit a decimal separator or the end of the number
					for(; i<buflen; i++)
					{
						if(!isdigit(tmp2[i]))
							break;
						out += '0';
					}
				}

				break;
			}
		}
	}

	//Special case: don't display negative zero
	if(out == "-0")
		out = "0";

	//Final formatting
	if(m_type != Unit::UNIT_UI)
		out += " ";
	out += prefix;
	out += suffix;

	/*
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
					snprintf(tmp, sizeof(tmp), format, value_rescaled, space, prefix.c_str(), suffix.c_str());
				}

				//If not a round number, add more digits (up to 4)
				else
				{
					if( fabs(round(value_rescaled) - value_rescaled) < 0.001 )
						snprintf(tmp, sizeof(tmp), "%.0f%s%s%s", value_rescaled, space, prefix.c_str(), suffix.c_str());
					else if(fabs(round(value_rescaled*10) - value_rescaled*10) < 0.001)
						snprintf(tmp, sizeof(tmp), "%.1f%s%s%s", value_rescaled, space, prefix.c_str(), suffix.c_str());
					else if(fabs(round(value_rescaled*100) - value_rescaled*100) < 0.001 )
						snprintf(tmp, sizeof(tmp), "%.2f%s%s%s", value_rescaled, space, prefix.c_str(), suffix.c_str());
					else if(fabs(round(value_rescaled*1000) - value_rescaled*1000) < 0.001 )
						snprintf(tmp, sizeof(tmp), "%.3f%s%s%s", value_rescaled, space, prefix.c_str(), suffix.c_str());
					else
						snprintf(tmp, sizeof(tmp), "%.4f%s%s%s", value_rescaled, space, prefix.c_str(), suffix.c_str());
				}
			}
			break;
	}
	*/

	SetDefaultLocale();
	return out;
}

/**
	@brief Parses a string based on the supplied unit

	@param str					The string to parse
	@param useDisplayLocale		True if the string is formatted for display (user's locale)
								False if the string is formatted for serialization ("C" locale regardless of user pref)
 */
double Unit::ParseString(const string& str, bool useDisplayLocale)
{
	if(useDisplayLocale)
		SetPrintingLocale();

	//Find the first non-numeric character in the strnig
	double scale = 1;
	for(size_t i=0; i<str.size(); i++)
	{
		char c = str[i];
		if(isspace(c) || isdigit(c) || (c == '.') || (c == ',') || (c == '-') )
			continue;

		if(c == 'T')
			scale = 1e12;
		else if(c == 'G')
			scale = 1e9;
		else if(c == 'M')
			scale = 1e6;
		else if(c == 'K' || c == 'k')
			scale = 1e3;
		else if(c == 'm')
			scale = 1e-3;
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
