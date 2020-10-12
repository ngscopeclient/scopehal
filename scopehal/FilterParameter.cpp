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
	@brief Implementation of FilterParameter
 */

#include "scopehal.h"
#include "Filter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FilterParameter

FilterParameter::FilterParameter(ParameterTypes type, Unit unit)
	: m_type(type)
	, m_unit(unit)
	, m_intval(0)
	, m_floatval(0)
	, m_string("")
{

}

void FilterParameter::ParseString(const string& str)
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
			m_floatval = m_unit.ParseString(str);
			m_intval = m_floatval;
			break;

		case TYPE_FILENAME:
		case TYPE_STRING:
			m_intval = 0;
			m_floatval = 0;
			m_string = str;
			m_filenames.push_back(str);
			break;

		case TYPE_FILENAMES:
			{
				m_intval = 0;
				m_floatval = 0;
				m_string = "";

				//Split out semicolon-delimited filenames
				string tmp;
				for(size_t i=0; i<str.length(); i++)
				{
					if(str[i] ==';')
					{
						if(tmp.empty())
							continue;

						if(m_string == "")
							m_string = tmp;
						m_filenames.push_back(tmp);
						tmp = "";
						continue;
					}

					tmp += str[i];
				}
				if(tmp != "")
				{
					m_string = tmp;
					m_filenames.push_back(tmp);
				}
			}
			break;

		case TYPE_ENUM:
			m_intval = 0;
			m_floatval = 0;
			m_string = str;
			m_filenames.push_back(str);

			if(m_forwardEnumMap.find(str) != m_forwardEnumMap.end())
				m_intval = m_forwardEnumMap[str];

			break;
	}
}

string FilterParameter::ToString()
{
	string ret;
	switch(m_type)
	{
		case TYPE_FLOAT:
			return m_unit.PrettyPrint(m_floatval);

		case TYPE_BOOL:
		case TYPE_INT:
			return m_unit.PrettyPrint(m_intval);

		case TYPE_FILENAME:
		case TYPE_STRING:
			return m_string;

		case TYPE_FILENAMES:
			for(auto f : m_filenames)
			{
				if(ret != "")
					ret += ";";
				ret += f;
			}
			return ret;

		case TYPE_ENUM:
			return m_reverseEnumMap[m_intval];

		default:
			return "unimplemented";
	}
}

int64_t FilterParameter::GetIntVal()
{
	return m_intval;
}

float FilterParameter::GetFloatVal()
{
	return m_floatval;
}

string FilterParameter::GetFileName()
{
	return m_string;
}

vector<string> FilterParameter::GetFileNames()
{
	return m_filenames;
}

void FilterParameter::SetIntVal(int64_t i)
{
	m_intval = i;
	m_floatval = i;
	m_string = "";
	m_filenames.clear();

	if(m_reverseEnumMap.find(i) != m_reverseEnumMap.end())
	{
		m_string = m_reverseEnumMap[i];
		m_filenames.push_back(m_string);
	}
}

void FilterParameter::SetFloatVal(float f)
{
	m_intval = f;
	m_floatval = f;
	m_string = "";
	m_filenames.clear();
}

void FilterParameter::SetFileName(const string& f)
{
	m_intval = 0;
	m_floatval = 0;
	m_string = f;
	m_filenames.clear();
	m_filenames.push_back(f);
}

void FilterParameter::SetFileNames(const vector<string>& names)
{
	m_intval = 0;
	m_floatval = 0;
	if(names.empty())
		m_string = "";
	else
		m_string = names[0];
	m_filenames = names;
}
