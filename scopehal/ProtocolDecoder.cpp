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
	//Look up the last character of the string and see if there's a SI scaling factor
	float scale = 1;
	if(str != "")
	{
		char suffix = str[str.size()-1];
		if(suffix == 'G')
			scale = 1000000000.0f;
		else if(suffix == 'M')
			scale = 1000000.0f;
		else if(suffix == 'K')
			scale = 1000.0f;
		else if(suffix == 'm')
			scale = 0.001f;
		else if(suffix == 'u')	//TODO: handle μ
			scale = 0.000001f;
	}

	switch(m_type)
	{
		case TYPE_BOOL:
			m_filename = "";
			if( (str == "1") || (str == "true") )
				m_intval = 1;
			else
				m_intval = 0;
			m_floatval = m_intval;
			break;
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

	m_intval *= scale;
	m_floatval *= scale;
}

string ProtocolDecoderParameter::ToString()
{
	char str_out[20];
	switch(m_type)
	{
		case TYPE_FLOAT:
			if(fabs(m_floatval) > 1000000000.0f)
				snprintf(str_out, sizeof(str_out), "%f G", m_floatval / 1000000000.0f);
			else if(fabs(m_floatval) > 1000000.0f)
				snprintf(str_out, sizeof(str_out), "%f M", m_floatval / 1000000.0f);
			else if(fabs(m_floatval) > 1000.0f)
				snprintf(str_out, sizeof(str_out), "%f k", m_floatval / 1000.0f);
			else
				snprintf(str_out, sizeof(str_out), "%f", m_floatval);
			break;
		case TYPE_BOOL:
		case TYPE_INT:
			if(fabs(m_intval) > 1000000000.0f)
				snprintf(str_out, sizeof(str_out), "%f G", m_intval / 1000000000.0f);
			else if(fabs(m_intval) > 1000000.0f)
				snprintf(str_out, sizeof(str_out), "%f M", m_intval / 1000000.0f);
			else if(fabs(m_intval) > 1000.0f)
				snprintf(str_out, sizeof(str_out), "%f k", m_intval / 1000.0f);
			else
				snprintf(str_out, sizeof(str_out), "%d", m_intval);
			break;
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
	OscilloscopeChannel::ChannelType type,
	string color,
	Category cat)
	: OscilloscopeChannel(NULL, "", type, color, 1)	//TODO: handle this better?
	, m_category(cat)
	, m_dirty(true)
{
	m_physical = false;
}

ProtocolDecoder::~ProtocolDecoder()
{
	for(auto c : m_channels)
	{
		if(c != NULL)
			c->Release();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void ProtocolDecoder::AddRef()
{
	m_refcount ++;
	for(auto c : m_channels)
	{
		if(c != NULL)
			c->AddRef();
	}
}

void ProtocolDecoder::Release()
{
	m_refcount --;
	if(m_refcount == 0)
		delete this;
}

bool ProtocolDecoder::IsOverlay()
{
	return true;
}

ProtocolDecoderParameter& ProtocolDecoder::GetParameter(string s)
{
	if(m_parameters.find(s) == m_parameters.end())
		LogError("Invalid parameter name");

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
		LogError("Invalid channel index");
		return "";
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
			LogError("Invalid channel format");
		}
		m_channels[i] = channel;
	}
	else
	{
		LogError("Invalid channel index");
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
	LogError("Invalid channel name");
}

OscilloscopeChannel* ProtocolDecoder::GetInput(size_t i)
{
	if(i < m_signalNames.size())
		return m_channels[i];
	else
	{
		LogError("Invalid channel index");
		return NULL;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Refreshing

void ProtocolDecoder::RefreshInputsIfDirty()
{
	for(auto c : m_channels)
	{
		if(!c)
			continue;
		auto decode = dynamic_cast<ProtocolDecoder*>(c);
		if(decode)
			decode->RefreshIfDirty();
	}
}

void ProtocolDecoder::RefreshIfDirty()
{
	if(m_dirty)
	{
		RefreshInputsIfDirty();
		Refresh();
		m_dirty = false;
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

ProtocolDecoder* ProtocolDecoder::CreateDecoder(string protocol, string color)
{
	if(m_createprocs.find(protocol) != m_createprocs.end())
		return m_createprocs[protocol](color);

	LogError("Invalid decoder name");
	return NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sampling helpers

/**
	@brief Samples a digital waveform on the rising edges of a clock

	The sampling rate of the data and clock signals need not be equal or uniform.

	The sampled waveform has a time scale in picoseconds regardless of the incoming waveform's time scale.

	@param data		The data signal to sample
	@param clock	The clock signal to use
	@param samples	Output waveform
 */
void ProtocolDecoder::SampleOnRisingEdges(DigitalCapture* data, DigitalCapture* clock, vector<DigitalSample>& samples)
{
	samples.clear();

	size_t ndata = 0;
	for(size_t i=1; i<clock->m_samples.size(); i++)
	{
		//Throw away clock samples until we find a rising edge
		auto csample = clock->m_samples[i];
		auto ocsample = clock->m_samples[i-1];
		if(!(csample.m_sample && !ocsample.m_sample))
			continue;

		//Throw away data samples until the data is synced with us
		int64_t clkstart = csample.m_offset * clock->m_timescale;
		while( (ndata < data->m_samples.size()) && (data->m_samples[ndata].m_offset * data->m_timescale < clkstart) )
			ndata ++;
		if(ndata >= data->m_samples.size())
			break;

		//Extend the previous sample's duration (if any) to our start
		if(samples.size())
		{
			auto& s = samples[samples.size() - 1];
			s.m_duration = clkstart - s.m_offset;
		}

		//Add the new sample
		samples.push_back(DigitalSample(clkstart, 1, data->m_samples[ndata].m_sample));
	}
}

/**
	@brief Samples a digital waveform on the falling edges of a clock

	The sampling rate of the data and clock signals need not be equal or uniform.

	The sampled waveform has a time scale in picoseconds regardless of the incoming waveform's time scale.

	@param data		The data signal to sample
	@param clock	The clock signal to use
	@param samples	Output waveform
 */
void ProtocolDecoder::SampleOnFallingEdges(DigitalCapture* data, DigitalCapture* clock, vector<DigitalSample>& samples)
{
	samples.clear();

	size_t ndata = 0;
	for(size_t i=1; i<clock->m_samples.size(); i++)
	{
		//Throw away clock samples until we find a falling edge
		auto csample = clock->m_samples[i];
		auto ocsample = clock->m_samples[i-1];
		if(!(!csample.m_sample && ocsample.m_sample))
			continue;

		//Throw away data samples until the data is synced with us
		int64_t clkstart = csample.m_offset * clock->m_timescale;
		while( (ndata < data->m_samples.size()) && (data->m_samples[ndata].m_offset * data->m_timescale < clkstart) )
			ndata ++;
		if(ndata >= data->m_samples.size())
			break;

		//Extend the previous sample's duration (if any) to our start
		if(samples.size())
		{
			auto& s = samples[samples.size() - 1];
			s.m_duration = clkstart - s.m_offset;
		}

		samples.push_back(DigitalSample(clkstart, 1, data->m_samples[ndata].m_sample));
	}
}

/**
	@brief Find zero crossings in a waveform, interpolating as necessary
 */
void ProtocolDecoder::FindZeroCrossings(AnalogCapture* data, float threshold, std::vector<int64_t>& edges)
{
	//Find times of the zero crossings (TODO: extract this into reusable function)
	bool first = true;
	bool last = false;
	for(size_t i=1; i<data->m_samples.size(); i++)
	{
		auto sin = data->m_samples[i];
		bool value = static_cast<float>(sin) > threshold;

		//Start time of the sample, in picoseconds
		int64_t t = data->m_triggerPhase + data->m_timescale * sin.m_offset;

		//Move to the middle of the sample
		t += data->m_timescale/2;

		//Save the last value
		if(first)
		{
			last = value;
			first = false;
			continue;
		}

		//Skip samples with no transition
		if(last == value)
			continue;

		//Interpolate the time
		t += data->m_timescale * Measurement::InterpolateTime(data, i-1, threshold);
		edges.push_back(t);
		last = value;
	}
}
