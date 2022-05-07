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
{

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
	switch(m_type)
	{
		case TYPE_BOOL:
			m_string = "";
			if( (str == "1") || (str == "true") )
				m_intval = 1;
			else
				m_intval = 0;

			m_floatval = m_intval;
			break;

		//Parse both int and float as float
		//so e.g. 1.5M parses correctly
		case TYPE_FLOAT:
		case TYPE_INT:
			m_floatval = m_unit.ParseString(str, useDisplayLocale);
			m_intval = m_floatval;
			break;

		case TYPE_FILENAME:
		case TYPE_STRING:
			m_intval = 0;
			m_floatval = 0;
			m_string = str;
			break;

		case TYPE_ENUM:
			m_intval = 0;
			m_floatval = 0;
			m_string = str;

			if(m_forwardEnumMap.find(str) != m_forwardEnumMap.end())
				m_intval = m_forwardEnumMap[str];

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
			return m_unit.PrettyPrint(m_intval, -1, useDisplayLocale);

		case TYPE_FILENAME:
		case TYPE_STRING:
			return m_string;

		case TYPE_ENUM:
			return m_reverseEnumMap.at(m_intval);

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

	m_changeSignal.emit();
}
