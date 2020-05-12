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

using namespace std;

string Unit::PrettyPrint(double value)
{
	const char* scale = "";
	const char* unit = "";

	double value_rescaled = value;

	//Default scaling and prefixes for SI base units
	if(fabs(value) > 1e9f)
	{
		value_rescaled /= 1e9;
		scale = "G";
	}
	else if(fabs(value) > 1e6)
	{
		value_rescaled /= 1e6;
		scale = "M";
	}
	else if(fabs(value) > 1e3)
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
		scale = "p";
	}

	switch(m_type)
	{
		//Special handling needed since it's not a SI base unit
		case UNIT_PS:
			unit = "s";

			if(fabs(value) >= 1e12)
			{
				value_rescaled = value / 1e12;
				scale = "";
			}
			else if(fabs(value) >= 1e9)
			{
				value_rescaled = value / 1e9;
				scale = "m";
			}
			else if(fabs(value) >= 1e6)
			{
				value_rescaled = value / 1e6;
				scale = "μ";
			}
			else if(fabs(value) >= 1e3)
			{
				value_rescaled = value / 1e3;
				scale = "n";
			}
			else
			{
				value_rescaled = value;
				scale = "p";
			}
			break;

		case UNIT_HZ:
			unit = "Hz";
			break;

		case UNIT_VOLTS:
			unit = "V";
			break;

		case UNIT_AMPS:
			unit = "A";
			break;

		case UNIT_OHMS:
			unit = "Ω";
			break;

		case UNIT_BITRATE:
			unit = "bps";
			break;

		//Dimensionless unit, no scaling applied
		case UNIT_PERCENT:
			unit = "%";
			scale = "";
			value_rescaled = value;
			break;
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
	if(m_type == UNIT_LOG_BER)		//special formatting for BER since it's already logarithmic
		snprintf(tmp, sizeof(tmp), "1e%.0f", value);
	else
		snprintf(tmp, sizeof(tmp), "%.3f %s%s", value_rescaled, scale, unit);
	return string(tmp);
}
