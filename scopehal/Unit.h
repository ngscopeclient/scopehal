/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2020 Andrew D. Zonenberg                                                                          *
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
 */

#ifndef Unit_h
#define Unit_h

/**
	@brief A unit of measurement, plus conversion to pretty-printed output
 */
class Unit
{
public:

	enum UnitType
	{
		UNIT_PS,			//Time. Note that this is not a SI base unit.
							//Using picoseconds allows integer math for most timebases, which keeps things nice and simple.
		UNIT_HZ,			//Frequency
		UNIT_VOLTS,			//Voltage
		UNIT_AMPS,			//Current
		UNIT_OHMS,			//Resistance
		UNIT_BITRATE,		//Bits per second
		UNIT_PERCENT,		//Dimensionless ratio
		UNIT_DB,			//Dimensionless ratio
		UNIT_COUNTS,		//Dimensionless ratio (histogram)
		UNIT_LOG_BER,		//Dimensionless ratio (log scale)
		UNIT_SAMPLERATE,	//Sample rate (Hz but displayed as S/s)
		UNIT_SAMPLEDEPTH	//Memory depth (number of samples)

		//TODO: more here
	};

	Unit(Unit::UnitType t)
	: m_type(t)
	{}

	std::string PrettyPrint(double value);

	UnitType GetType()
	{ return m_type; }

	bool operator==(const Unit& rhs)
	{ return m_type == rhs.m_type; }

	bool operator!=(const Unit& rhs)
	{ return m_type != rhs.m_type; }

	bool operator!=(UnitType rhs)
	{ return m_type != rhs; }

protected:
	UnitType m_type;
};

#endif
