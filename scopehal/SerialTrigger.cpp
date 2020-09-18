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

#include "scopehal.h"
#include "SerialTrigger.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SerialTrigger::SerialTrigger(Oscilloscope* scope)
	: Trigger(scope)
{
	//CreateInput("din");

	m_radixname = "Radix";
	m_parameters[m_radixname] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_radixname].AddEnumValue("ASCII", RADIX_ASCII);
	m_parameters[m_radixname].AddEnumValue("Binary", RADIX_BINARY);
	m_parameters[m_radixname].AddEnumValue("Hex", RADIX_HEX);

	m_patternname = "Pattern";
	m_parameters[m_patternname] = FilterParameter(FilterParameter::TYPE_STRING, Unit(Unit::UNIT_COUNTS));

	m_pattern2name = "Pattern 2";
	m_parameters[m_pattern2name] = FilterParameter(FilterParameter::TYPE_STRING, Unit(Unit::UNIT_COUNTS));

	m_conditionname = "Condition";
	m_parameters[m_conditionname] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_conditionname].AddEnumValue("==", CONDITION_EQUAL);
	m_parameters[m_conditionname].AddEnumValue("!=", CONDITION_NOT_EQUAL);
	m_parameters[m_conditionname].AddEnumValue("<", CONDITION_LESS);
	m_parameters[m_conditionname].AddEnumValue("<=", CONDITION_LESS_OR_EQUAL);
	m_parameters[m_conditionname].AddEnumValue(">", CONDITION_GREATER);
	m_parameters[m_conditionname].AddEnumValue(">=", CONDITION_GREATER_OR_EQUAL);
	m_parameters[m_conditionname].AddEnumValue("Between", CONDITION_BETWEEN);
	m_parameters[m_conditionname].AddEnumValue("Not Between", CONDITION_NOT_BETWEEN);
}

SerialTrigger::~SerialTrigger()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

/**
	@brief Converts a pattern from ASCII ternary (0-1-x) to a more display-friendly format
 */
void SerialTrigger::SetPatterns(string p1, string p2, bool ignore_p2)
{
	//Try to figure out the best radix to use for display.
	bool has_xs = false;
	bool has_unaligned_xs = false;
	bool has_nonprint = false;

	size_t len = min(p1.length(), p2.length());
	uint8_t current_byte_1 = 0;
	uint8_t current_byte_2 = 0;
	size_t had_xs = false;
	string tmp1;
	string tmp2;
	string h1;
	string h2;
	bool nibble_x = false;
	for(size_t i=0; i<len; i++)
	{
		char c1 = p1[i];
		char c2 = p2[i];

		//Look for 'x' bits in pattern 1
		//(pattern 2 can't be Xs, it makes no sense to check if a value is between two partially defined patterns)
		if(tolower(c1) == 'x')
		{
			nibble_x = true;

			//If we are starting a block of Xs at an unaligned byte address, flag that
			if(!had_xs && ( (i & 7) != 0) )
				has_unaligned_xs = true;

			has_xs = true;
		}
		else
		{
			//If we are ending a block of Xs at an unaligned byte address, flag that
			if(had_xs && ( (i & 7) != 7) )
				has_unaligned_xs = true;
		}
		had_xs = has_xs;

		//Get current byte values
		current_byte_1 <<= 1;
		current_byte_2 <<= 1;
		if(c1 == '1')
			current_byte_1 |= 1;
		if(c2 == '1')
			current_byte_2 |= 1;

		//Each nibble, save nibble data as hex
		if( (i&3) == 3 )
		{
			//input 1 can be X too
			if(nibble_x)
				h1 += 'x';
			else
			{
				int nib1 = current_byte_1 & 7;
				if(nib1 > 9)
					h1 += 'a' + (nib1 - 0xa);
				else
					h1 += '0' + nib1;
			}

			//input 2 is only binary
			int nib2 = current_byte_2 & 7;
			if(nib2 > 9)
				h2 += 'a' + (nib2 - 0xa);
			else
				h2 += '0' + nib2;
		}

		//Each byte, look for nonprintable characters
		if( (i&7) == 7 )
		{
			tmp1 += current_byte_1;
			tmp2 += current_byte_2;

			if( !isprint(current_byte_1) || (!ignore_p2 && !isprint(current_byte_2) ) )
				has_nonprint = true;
		}
	}

	//Only printable characters? Use ASCII
	if(!has_xs && !has_nonprint)
	{
		SetRadix(RADIX_ASCII);
		m_parameters[m_patternname].ParseString(tmp1);
		if(ignore_p2)
			m_parameters[m_pattern2name].ParseString("");
		else
			m_parameters[m_pattern2name].ParseString(tmp2);
	}

	//No Xs, or aligned Xs? Use hex
	else if(!has_unaligned_xs)
	{
		SetRadix(RADIX_HEX);
		m_parameters[m_patternname].ParseString(h1);
		if(ignore_p2)
			m_parameters[m_pattern2name].ParseString("");
		else
			m_parameters[m_pattern2name].ParseString(h2);
	}

	//Unaligned X bits, use binary
	else
	{
		SetRadix(RADIX_BINARY);
		m_parameters[m_patternname].ParseString(p1);
		if(ignore_p2)
			m_parameters[m_pattern2name].ParseString("");
		else
			m_parameters[m_pattern2name].ParseString(p2);
	}
}

/**
	@brief Converts a pattern in the current radix back to ASCII ternary
 */
string SerialTrigger::FormatPattern(string str)
{
	string ret;

	switch(GetRadix())
	{
		case RADIX_ASCII:
			{
				//ASCII pattern. Output binary data for each byte, MSB first
				for(size_t i=0; i<str.length(); i++)
				{
					char c = str[i];
					for(size_t j=0; j<8; j++)
					{
						if(c & 0x80)
							ret += '1';
						else
							ret += '0';
						c <<= 1;
					}
				}
			}
			break;

		case RADIX_HEX:
			break;

		case RADIX_BINARY:
		default:
			break;
	}

	return ret;
}
