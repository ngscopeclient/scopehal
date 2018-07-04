/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2017 Andrew D. Zonenberg                                                                          *
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
	@brief Implementation of ProtocolDecoder
 */

#include "scopehal.h"
#include "ProtocolDecoder.h"

ProtocolDecoder::CreateMapType ProtocolDecoder::m_createprocs;

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ProtocolDecoderParameter
ProtocolDecoderParameter::ProtocolDecoderParameter(ParameterTypes type)
	: m_type(type)
{
	m_intval = 0;
	m_floatval = 0;
	m_filename = "";
}

void ProtocolDecoderParameter::ParseString(string str)
{
	switch(m_type)
	{
		case TYPE_FLOAT:
			sscanf(str.c_str(), "%20f", &m_floatval);
			m_intval = m_floatval;
			m_filename = "";
			break;
		case TYPE_INT:
			sscanf(str.c_str(), "%10d", &m_intval);
			m_floatval = m_intval;
			m_filename = "";
			break;
		case TYPE_FILENAME:
			m_intval = 0;
			m_floatval = 0;
			m_filename = str;
			break;
	}
}

string ProtocolDecoderParameter::ToString()
{
	char str_out[20];
	switch(m_type)
	{
		case TYPE_FLOAT:
			snprintf(str_out, sizeof(str_out), "%f", m_floatval);
			break;
		case TYPE_INT:
			snprintf(str_out, sizeof(str_out), "%d", m_intval);
			break;
		case TYPE_FILENAME:
			return m_filename;
			break;
	}
	return str_out;
}

int ProtocolDecoderParameter::GetIntVal()
{
	return m_intval;
}

float ProtocolDecoderParameter::GetFloatVal()
{
	return m_floatval;
}

string ProtocolDecoderParameter::GetFileName()
{
	return m_filename;
}

void ProtocolDecoderParameter::SetIntVal(int i)
{
	m_intval = i;
	m_floatval = i;
	m_filename = "";
}

void ProtocolDecoderParameter::SetFloatVal(float f)
{
	m_intval = f;
	m_floatval = f;
	m_filename = "";
}

void ProtocolDecoderParameter::SetFileName(string f)
{
	m_intval = 0;
	m_floatval = 0;
	m_filename = f;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ProtocolDecoder::ProtocolDecoder(
	string hwname,
	OscilloscopeChannel::ChannelType type,
	string color)
	: OscilloscopeChannel(hwname, type, color)
{
}

ProtocolDecoder::~ProtocolDecoder()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

bool ProtocolDecoder::IsOverlay()
{
	return true;
}

ProtocolDecoderParameter& ProtocolDecoder::GetParameter(string s)
{
	if(m_parameters.find(s) == m_parameters.end())
	{
		throw JtagExceptionWrapper(
			"Invalid parameter name",
			"");
	}

	return m_parameters[s];
}

size_t ProtocolDecoder::GetInputCount()
{
	return m_signalNames.size();
}

string ProtocolDecoder::GetInputName(size_t i)
{
	if(i < m_signalNames.size())
		return m_signalNames[i];
	else
	{
		throw JtagExceptionWrapper(
			"Invalid channel index",
			"");
	}
}

void ProtocolDecoder::SetInput(size_t i, OscilloscopeChannel* channel)
{
	if(i < m_signalNames.size())
	{
		if(channel == NULL)	//NULL is always legal
		{
			m_channels[i] = NULL;
			return;
		}
		if(!ValidateChannel(i, channel))
		{
			throw JtagExceptionWrapper(
				"Invalid channel format",
				"");
		}
		m_channels[i] = channel;
	}
	else
	{
		throw JtagExceptionWrapper(
			"Invalid channel index",
			"");
	}
}

void ProtocolDecoder::SetInput(string name, OscilloscopeChannel* channel)
{
	//Find the channel
	for(size_t i=0; i<m_signalNames.size(); i++)
	{
		if(m_signalNames[i] == name)
		{
			SetInput(i, channel);
			return;
		}
	}

	//Not found
	throw JtagExceptionWrapper(
		"Invalid channel name",
		"");
}

OscilloscopeChannel* ProtocolDecoder::GetInput(size_t i)
{
	if(i < m_signalNames.size())
		return m_channels[i];
	else
	{
		throw JtagExceptionWrapper(
			"Invalid channel index",
			"");
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Enumeration

void ProtocolDecoder::AddDecoderClass(string name, CreateProcType proc)
{
	m_createprocs[name] = proc;
}

void ProtocolDecoder::EnumProtocols(vector<string>& names)
{
	for(CreateMapType::iterator it=m_createprocs.begin(); it != m_createprocs.end(); ++it)
		names.push_back(it->first);
}

ProtocolDecoder* ProtocolDecoder::CreateDecoder(string protocol, string hwname, string color)
{
	if(m_createprocs.find(protocol) != m_createprocs.end())
		return m_createprocs[protocol](hwname, color);

	throw JtagExceptionWrapper(
		"Invalid decoder name",
		"");
}
