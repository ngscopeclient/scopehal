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
	@author Andrew D. Zonenberg
	@brief Declaration of Unit
	@ingroup datamodel
 */

#ifndef Unit_h
#define Unit_h

#ifndef _WIN32
#include <locale.h>

#if defined(__APPLE__) || defined(__FreeBSD__)
#include <xlocale.h>
#endif

#endif

#define UNIT_OVERLOAD_LABEL "Overload"

/**
	@brief A unit of measurement, plus conversion to pretty-printed output

	TODO: add scale factors too?

	@ingroup datamodel
 */
class Unit
{
public:

	enum UnitType
	{
		UNIT_FS,			//Time. Note that this is not a SI base unit.
							//Using femtoseconds allows integer math for all known scope timebases,
							//which keeps things nice and simple.
		UNIT_HZ,			//Frequency
		UNIT_VOLTS,			//Voltage
		UNIT_AMPS,			//Current
		UNIT_OHMS,			//Resistance
		UNIT_BITRATE,		//Bits per second
		UNIT_PERCENT,		//Dimensionless ratio
		UNIT_DB,			//Dimensionless ratio
		UNIT_DBM,			//dB mW (more common than dBW)
		UNIT_COUNTS,		//Dimensionless ratio (histogram)
		UNIT_COUNTS_SCI,	//Dimensionless ratio (histogram, but scientific notation)
		UNIT_LOG_BER,		//Dimensionless ratio (value is a logarithm)
		UNIT_RATIO_SCI,		//Dimensionless ratio (scientific notation)
		UNIT_SAMPLERATE,	//Sample rate (Hz but displayed as S/s)
		UNIT_SAMPLEDEPTH,	//Memory depth (number of samples)
		UNIT_WATTS,			//Power
		UNIT_UI,			//Unit interval (relative to signal bit rate)
		UNIT_DEGREES,		//Angular degrees
		UNIT_RPM,			//Revolutions per minute
		UNIT_CELSIUS,		//Degrees Celsius
		UNIT_RHO,			//Reflection coefficient (dimensionless ratio)
		UNIT_HEXNUM,		//Hexadecimal address or similar
		UNIT_PM,			//Distance or wavelength.
							//As with femtoseconds, this provides a reasonable range
							//(1 picometer to +/- 9223 km) of distances using int64's.

		UNIT_MILLIVOLTS,	//Hack needed for voltage in the X axis since we use integer coordinates there
		UNIT_MICROVOLTS,	//Hack needed for voltage in the X axis since we use integer coordinates there
		UNIT_VOLT_SEC,      //Hack needed to measure area under the curve in terms of volt-seconds

		UNIT_BYTES,			//used mostly for displaying memory usage

		//Did I mention we really need proper algebraic unit support?
		UNIT_W_M2_NM,		//absolute spectral irradiance
							//(scale by 100 to get uW/cm^2/nm)

		UNIT_W_M2,			//absolute irradiance
							//(scale by 100 to get uW/cm^2)

		UNIT_MICROAMPS,		//Another hack for current in the X axis

		UNIT_FARADS,		//Capacitance in farads

		//TODO: more here
	};

	Unit(Unit::UnitType t = UNIT_COUNTS)
	: m_type(t)
	{}

	Unit(const std::string& rhs);
	std::string ToString() const;

	std::string PrettyPrint(double value, int sigfigs = -1, bool useDisplayLocale = true) const;
	std::string PrettyPrintInt64(int64_t value, int sigfigs = -1, bool useDisplayLocale = true) const;

	std::string PrettyPrintRange(double pixelMin, double pixelMax, double rangeMin, double rangeMax) const;

	double ParseString(const std::string& str, bool useDisplayLocale = true);
	int64_t ParseStringInt64(const std::string& str, bool useDisplayLocale = true);

	UnitType GetType()
	{ return m_type; }

	bool operator==(const Unit& rhs)
	{ return m_type == rhs.m_type; }

	bool operator!=(const Unit& rhs)
	{ return m_type != rhs.m_type; }

	bool operator!=(UnitType rhs)
	{ return m_type != rhs; }

	Unit operator*(const Unit& rhs);
	Unit operator/(const Unit& rhs);

	static void SetLocale(const char* locale);

protected:
	UnitType m_type;

	void GetSIScalingFactor(double num, double& scaleFactor, std::string& prefix) const;
	void GetUnitSuffix(UnitType type, double num, double& scaleFactor, std::string& prefix, std::string& numprefix, std::string& suffix) const;

#ifdef _WIN32
	/**
		@brief String form of m_locale for use on Windows
	 */
	static std::string m_slocale;

#else
	/**
		@brief The user's requested locale for display
	 */
	static locale_t m_locale;

	/**
		@brief Handle to the "C" locale, used for interchange
	 */
	static locale_t m_defaultLocale;
#endif

	static void SetPrintingLocale();
	static void SetDefaultLocale();
};

#endif
