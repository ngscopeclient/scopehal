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

#include "scopehal.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

CANChannel::CANChannel(
	Oscilloscope* scope,
	const string& hwname,
	const string& color,
	size_t index)
	: OscilloscopeChannel(scope, hwname, color, Unit(Unit::UNIT_FS), index)
{
	ClearStreams();
	AddStream(Unit(Unit::UNIT_COUNTS), "canbus", Stream::STREAM_TYPE_PROTOCOL);
}

CANChannel::~CANChannel()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CANWaveform

string CANWaveform::GetColor(size_t i)
{
	const CANSymbol& s = m_samples[i];

	switch(s.m_stype)
	{
		case CANSymbol::TYPE_SOF:
			return StandardColors::colors[StandardColors::COLOR_PREAMBLE];

		case CANSymbol::TYPE_R0:
			if(!s.m_data)
				return StandardColors::colors[StandardColors::COLOR_PREAMBLE];
			else
				return StandardColors::colors[StandardColors::COLOR_ERROR];

		case CANSymbol::TYPE_ID:
			return StandardColors::colors[StandardColors::COLOR_ADDRESS];

		case CANSymbol::TYPE_RTR:
		case CANSymbol::TYPE_FD:
			return StandardColors::colors[StandardColors::COLOR_CONTROL];

		case CANSymbol::TYPE_DLC:
			if(s.m_data > 8)
				return StandardColors::colors[StandardColors::COLOR_ERROR];
			else
				return StandardColors::colors[StandardColors::COLOR_CONTROL];

		case CANSymbol::TYPE_DATA:
			return StandardColors::colors[StandardColors::COLOR_DATA];

		case CANSymbol::TYPE_CRC_OK:
			return StandardColors::colors[StandardColors::COLOR_CHECKSUM_OK];

		case CANSymbol::TYPE_CRC_DELIM:
		case CANSymbol::TYPE_ACK_DELIM:
		case CANSymbol::TYPE_EOF:
			if(s.m_data)
				return StandardColors::colors[StandardColors::COLOR_PREAMBLE];
			else
				return StandardColors::colors[StandardColors::COLOR_ERROR];

		case CANSymbol::TYPE_ACK:
			if(!s.m_data)
				return StandardColors::colors[StandardColors::COLOR_CHECKSUM_OK];
			else
				return StandardColors::colors[StandardColors::COLOR_CHECKSUM_BAD];

		case CANSymbol::TYPE_CRC_BAD:
		default:
			return StandardColors::colors[StandardColors::COLOR_ERROR];
	}
}

string CANWaveform::GetText(size_t i)
{
	const CANSymbol& s = m_samples[i];

	char tmp[32];
	switch(s.m_stype)
	{
		case CANSymbol::TYPE_SOF:
			return "SOF";

		case CANSymbol::TYPE_ID:
			snprintf(tmp, sizeof(tmp), "ID %03x", s.m_data);
			break;

		case CANSymbol::TYPE_FD:
			if(s.m_data)
				return "FD";
			else
				return "STD";

		case CANSymbol::TYPE_RTR:
			if(s.m_data)
				return "REQ";
			else
				return "DATA";

		case CANSymbol::TYPE_R0:
			return "RSVD";

		case CANSymbol::TYPE_DLC:
			snprintf(tmp, sizeof(tmp), "Len %u", s.m_data);
			break;

		case CANSymbol::TYPE_DATA:
			snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
			break;

		case CANSymbol::TYPE_CRC_OK:
		case CANSymbol::TYPE_CRC_BAD:
			snprintf(tmp, sizeof(tmp), "CRC: %04x", s.m_data);
			break;

		case CANSymbol::TYPE_CRC_DELIM:
			return "CRC DELIM";

		case CANSymbol::TYPE_ACK:
			if(!s.m_data)
				return "ACK";
			else
				return "NAK";

		case CANSymbol::TYPE_ACK_DELIM:
			return "ACK DELIM";

		case CANSymbol::TYPE_EOF:
			return "EOF";

		default:
			return "ERROR";
	}
	return string(tmp);
}
