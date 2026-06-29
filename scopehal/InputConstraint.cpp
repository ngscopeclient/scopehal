/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
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

InputConstraint::InputConstraint(FlowGraphNode* sink)
	: m_sink(sink)
{
}

InputConstraint::~InputConstraint()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Formatting helpers

string InputConstraint::StreamTypeToString(Stream::StreamType type)
{
	switch(type)
	{
		case Stream::STREAM_TYPE_ANALOG:			return "analog waveform";
		case Stream::STREAM_TYPE_DIGITAL:			return "digital waveform";
		case Stream::STREAM_TYPE_DIGITAL_BUS:		return "digital bus";
		case Stream::STREAM_TYPE_EYE:				return "eye pattern";
		case Stream::STREAM_TYPE_SPECTROGRAM:		return "spectrogram";
		case Stream::STREAM_TYPE_WATERFALL:			return "waterfall";
		case Stream::STREAM_TYPE_CONSTELLATION:		return "constellation";
		case Stream::STREAM_TYPE_TRIGGER:			return "trigger";
		case Stream::STREAM_TYPE_PROTOCOL:			return "protocol";
		case Stream::STREAM_TYPE_ANALOG_SCALAR:		return "analog scalar";
		case Stream::STREAM_TYPE_DIGITAL_SCALAR:	return "digital scalar";

		case Stream::STREAM_TYPE_UNDEFINED:
		default:
			return "undefined";
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// InputConstraintStreamTypes

string InputConstraintStreamTypes::ToString()
{
	string ret = "Stream type is ";

	size_t len = m_types.size();
	for(size_t i=0; i<len; i++)
	{
		if(i > 0)
		{
			if(len > 2)
				ret += ", ";
			if(i == (len-1))
				ret += " or ";
		}
		ret += StreamTypeToString(m_types[i]);
	}

	return ret;
}
