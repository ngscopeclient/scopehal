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
	@brief Implementation of FilterParameter
 */

#include "scopehal.h"
#include "Filter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FilterParameter

/**
	@brief Creates a parameter

	@param type	Type of parameter
	@param unit	Unit of measurement (ignored for non-numeric types)
 */
FilterParameter::FilterParameter(ParameterTypes type, Unit unit)
	: m_fileIsOutput(false)
	, m_type(type)
	, m_unit(unit)
	, m_intval(0)
	, m_floatval(0)
	, m_string("")
	, m_hidden(false)
	, m_readOnly(false)
{

}

/**
	@brief Constructs a FilterParameter object for choosing from available units
 */
FilterParameter FilterParameter::UnitSelector()
{
	FilterParameter ret(TYPE_ENUM, Unit::UNIT_COUNTS);
	ret.AddEnumValue("S", Unit::UNIT_FS);	//pretty-printed as seconds even though internally scaled to fs
	ret.AddEnumValue("m", Unit::UNIT_PM);	//pretty-printed as meters even though internally scaled to pm
	ret.AddEnumValue("Hz", Unit::UNIT_HZ);
	ret.AddEnumValue("V", Unit::UNIT_VOLTS);
	ret.AddEnumValue("A", Unit::UNIT_AMPS);
	ret.AddEnumValue("Ω", Unit::UNIT_OHMS);
	ret.AddEnumValue("Bps", Unit::UNIT_BITRATE);
	ret.AddEnumValue("%", Unit::UNIT_PERCENT);
	ret.AddEnumValue("dB", Unit::UNIT_DB);
	ret.AddEnumValue("dBm", Unit::UNIT_DBM);
	ret.AddEnumValue("Dimensionless", Unit::UNIT_COUNTS);
	ret.AddEnumValue("Dimensionless (log)", Unit::UNIT_COUNTS_SCI);
	ret.AddEnumValue("Log BER", Unit::UNIT_LOG_BER);
	ret.AddEnumValue("Sa/s", Unit::UNIT_SAMPLERATE);
	ret.AddEnumValue("Samples", Unit::UNIT_SAMPLEDEPTH);
	ret.AddEnumValue("W", Unit::UNIT_WATTS);
	ret.AddEnumValue("UI", Unit::UNIT_UI);
	ret.AddEnumValue("° (angular)", Unit::UNIT_DEGREES);
	ret.AddEnumValue("RPM", Unit::UNIT_RPM);
	ret.AddEnumValue("°C", Unit::UNIT_CELSIUS);
	ret.AddEnumValue("ρ", Unit::UNIT_RHO);
	ret.AddEnumValue("Hexadecimal", Unit::UNIT_HEXNUM);
	ret.AddEnumValue("mV", Unit::UNIT_MILLIVOLTS);
	ret.AddEnumValue("V/s", Unit::UNIT_VOLT_SEC);
	return ret;
}

/**
	@brief Reinterprets our string representation (in case our type or enums changed)
 */
void FilterParameter::Reinterpret()
{
	ParseString(m_string);
}

/**
	@brief Sets the parameter to a value represented as a string.

	The string is converted to the appropriate internal representation.
 */
void FilterParameter::ParseString(const string& str, bool useDisplayLocale)
{
	//Default conversions
	m_8b10bPattern.clear();
	m_intval = 0;
	m_floatval = 0;
	m_string = str;

	//Handle specific types
	switch(m_type)
	{
		case TYPE_BOOL:
			if( (str == "1") || (str == "true") )
				m_intval = 1;
			else
				m_intval = 0;

			m_floatval = m_intval;
			break;

		case TYPE_FLOAT:
			m_floatval = m_unit.ParseString(str, useDisplayLocale);
			m_intval = m_floatval;
			break;

		case TYPE_INT:
			//If there's a decimal point parse it as a float
			//so e.g. "1.5M" parses correctly
			//TODO: instead, multiply by an integer scaling factor and strip the decimal
			if(str.find(".") != string::npos)
			{
				m_floatval = m_unit.ParseString(str, useDisplayLocale);
				m_intval = m_floatval;
			}
			else
			{
				m_intval = m_unit.ParseStringInt64(str, useDisplayLocale);
				m_floatval = m_intval;
			}
			break;

		case TYPE_FILENAME:
		case TYPE_STRING:
			break;

		case TYPE_ENUM:
			if(m_forwardEnumMap.find(str) != m_forwardEnumMap.end())
				m_intval = m_forwardEnumMap[str];

			break;

		case TYPE_8B10B_PATTERN:
			{
				//Chunk the message up into blocks
				vector<string> blocks;
				string tmp;
				for(auto c : str)
				{
					if(isspace(c))
					{
						if(!tmp.empty())
							blocks.push_back(tmp);
						tmp = "";
					}
					else
						tmp += c;
				}
				if(!tmp.empty())
					blocks.push_back(tmp);

				//Parse each block
				m_8b10bPattern.resize(blocks.size());
				for(size_t i=0; i<blocks.size(); i++)
				{
					auto& b = blocks[i];

					//First character is type field
					if(b[0] == 'x')
					{
						m_8b10bPattern[i].ktype = T8B10BSymbol::DONTCARE;
						continue;
					}
					else if(b[0] == 'K')
						m_8b10bPattern[i].ktype = T8B10BSymbol::KSYMBOL;
					else //if(b[0] == 'D')
						m_8b10bPattern[i].ktype = T8B10BSymbol::DSYMBOL;

					//Parse the data byte
					int code5;
					int code3;
					char unused;
					char suffix;
					m_8b10bPattern[i].disparity = T8B10BSymbol::ANY;
					if(4 == sscanf(b.c_str(), "%c%d.%d%c", &unused, &code5, &code3, &suffix))
					{
						if(suffix == '+')
							m_8b10bPattern[i].disparity = T8B10BSymbol::POSITIVE;
						else if(suffix == '-')
							m_8b10bPattern[i].disparity = T8B10BSymbol::NEGATIVE;
					}
					else if(3 != sscanf(b.c_str(), "%c%d.%d", &unused, &code5, &code3))
						continue;

					m_8b10bPattern[i].value = (code3 << 5) | code5;
				}
			}
			break;
	}

	m_changeSignal.emit();
}

/**
	@brief Returns a pretty-printed representation of the parameter's value.
 */
string FilterParameter::ToString(bool useDisplayLocale) const
{
	string ret;
	switch(m_type)
	{
		case TYPE_FLOAT:
			return m_unit.PrettyPrint(m_floatval, -1, useDisplayLocale);

		case TYPE_BOOL:
		case TYPE_INT:
			return m_unit.PrettyPrintInt64(m_intval, -1, useDisplayLocale);

		case TYPE_FILENAME:
		case TYPE_STRING:
			return m_string;

		case TYPE_ENUM:
			{
				if(m_reverseEnumMap.find(m_intval) != m_reverseEnumMap.end())
					return m_reverseEnumMap.at(m_intval);
				else
					return "";
			}

		//Yay, complex formatting!
		case TYPE_8B10B_PATTERN:
			{
				for(auto p : m_8b10bPattern)
				{
					if(!ret.empty())
						ret += " ";

					if(p.ktype == T8B10BSymbol::DONTCARE)
					{
						ret += "x";
						continue;
					}
					else if(p.ktype == T8B10BSymbol::KSYMBOL)
						ret += "K";
					else
						ret += "D";

					ret += to_string(p.value & 0x1f) + '.' + to_string(p.value >> 5);

					switch(p.disparity)
					{
						case T8B10BSymbol::POSITIVE:
							ret += "+";
							break;

						case T8B10BSymbol::NEGATIVE:
							ret += "-";
							break;

						default:
							break;
					}
				}
				return ret;
			}

		default:
			return "unimplemented";
	}
}

/**
	@brief Sets the parameter to a boolean value
 */
void FilterParameter::SetBoolVal(bool b)
{
	m_intval = b;
	m_floatval = b;
	m_string = b ? "1" : "0";
	m_8b10bPattern.clear();

	m_changeSignal.emit();
}

/**
	@brief Sets the parameter to an integer value
 */
void FilterParameter::SetIntVal(int64_t i)
{
	m_intval = i;
	m_floatval = i;
	m_string = "";
	m_8b10bPattern.clear();

	if(m_reverseEnumMap.find(i) != m_reverseEnumMap.end())
		m_string = m_reverseEnumMap[i];

	m_changeSignal.emit();
}

/**
	@brief Sets the parameter to a floating point value
 */
void FilterParameter::SetFloatVal(float f)
{
	m_intval = f;
	m_floatval = f;
	m_string = "";
	m_8b10bPattern.clear();

	m_changeSignal.emit();
}

/**
	@brief Sets the parameter to a string
 */
void FilterParameter::SetStringVal(const string& f)
{
	SetFileName(f);
}

/**
	@brief Sets the parameter to a file path
 */
void FilterParameter::SetFileName(const string& f)
{
	m_intval = 0;
	m_floatval = 0;
	m_string = f;
	m_8b10bPattern.clear();

	m_changeSignal.emit();
}

void FilterParameter::Set8B10BPattern(const vector<T8B10BSymbol>& pattern)
{
	m_intval = 0;
	m_floatval = 0;
	m_8b10bPattern = pattern;
	m_string = ToString();

	m_changeSignal.emit();
}
