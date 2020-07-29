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
std::set<ProtocolDecoder*> ProtocolDecoder::m_decodes;

using namespace std;

Gdk::Color ProtocolDecoder::m_standardColors[STANDARD_COLOR_COUNT] =
{
	Gdk::Color("#336699"),	//COLOR_DATA
	Gdk::Color("#c000a0"),	//COLOR_CONTROL
	Gdk::Color("#ffff00"),	//COLOR_ADDRESS
	Gdk::Color("#808080"),	//COLOR_PREAMBLE
	Gdk::Color("#00ff00"),	//COLOR_CHECKSUM_OK
	Gdk::Color("#ff0000"),	//COLOR_CHECKSUM_BAD
	Gdk::Color("#ff0000"),	//COLOR_ERROR
	Gdk::Color("#404040")	//COLOR_IDLE
};

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
		else if(suffix == 'k')
			scale = 1000.0f;
		else if(suffix == 'm')
			scale = 0.001f;
		else if(suffix == 'u')	//TODO: handle μ
			scale = 1e-6f;
		else if(suffix == 'n')
			scale = 1e-9f;
		else if(suffix == 'p')
			scale = 1e-12f;
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

		//Parse both int and float as float
		//so e.g. 1.5M parses correctly
		case TYPE_FLOAT:
		case TYPE_INT:
			sscanf(str.c_str(), "%20f", &m_floatval);
			m_floatval *= scale;
			m_intval = m_floatval;
			m_filename = "";
			break;

		case TYPE_FILENAME:
		case TYPE_FILENAMES:
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
			if(fabs(m_floatval) > 1000000000.0f)
				snprintf(str_out, sizeof(str_out), "%f G", m_floatval / 1000000000.0f);
			else if(fabs(m_floatval) > 1000000.0f)
				snprintf(str_out, sizeof(str_out), "%f M", m_floatval / 1000000.0f);
			else if(fabs(m_floatval) > 1000.0f)
				snprintf(str_out, sizeof(str_out), "%f k", m_floatval / 1000.0f);
			else if(fabs(m_floatval) > 1)
				snprintf(str_out, sizeof(str_out), "%f", m_floatval);
			else if(fabs(m_floatval) > 1e-3)
				snprintf(str_out, sizeof(str_out), "%f m", m_floatval * 1e3f);
			else if(fabs(m_floatval) > 1e-6)
				snprintf(str_out, sizeof(str_out), "%f u", m_floatval * 1e6f);
			else if(fabs(m_floatval) > 1e-9)
				snprintf(str_out, sizeof(str_out), "%f n", m_floatval * 1e9f);
			else
				snprintf(str_out, sizeof(str_out), "%f p", m_floatval * 1e12f);
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
		case TYPE_FILENAMES:
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

vector<string> ProtocolDecoderParameter::GetFileNames()
{
	return m_filenames;
}

void ProtocolDecoderParameter::SetIntVal(int i)
{
	m_intval = i;
	m_floatval = i;
	m_filename = "";
	m_filenames.clear();
}

void ProtocolDecoderParameter::SetFloatVal(float f)
{
	m_intval = f;
	m_floatval = f;
	m_filename = "";
	m_filenames.clear();
}

void ProtocolDecoderParameter::SetFileName(string f)
{
	m_intval = 0;
	m_floatval = 0;
	m_filename = f;
	m_filenames.clear();
	m_filenames.push_back(f);
}

void ProtocolDecoderParameter::SetFileNames(vector<string> names)
{
	m_intval = 0;
	m_floatval = 0;
	if(names.empty())
		m_filename = "";
	else
		m_filename = names[0];
	m_filenames = names;
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
	m_decodes.emplace(this);
}

ProtocolDecoder::~ProtocolDecoder()
{
	m_decodes.erase(this);

	for(auto c : m_channels)
	{
		if(c != NULL)
			c->Release();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void ProtocolDecoder::ClearSweeps()
{
	//default no-op implementation
}

void ProtocolDecoder::AddRef()
{
	m_refcount ++;
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
		LogError("Invalid parameter name\n");

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
		LogError("Invalid channel index\n");
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
			LogError("Invalid channel format\n");
			//return;
		}

		if(m_channels[i] != NULL)
			m_channels[i]->Release();
		m_channels[i] = channel;
		channel->AddRef();
	}
	else
	{
		LogError("Invalid channel index\n");
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
	LogError("Invalid channel name\n");
}

OscilloscopeChannel* ProtocolDecoder::GetInput(size_t i)
{
	if(i < m_signalNames.size())
		return m_channels[i];
	else
	{
		LogError("Invalid channel index\n");
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

void ProtocolDecoder::DoAddDecoderClass(string name, CreateProcType proc)
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

	LogError("Invalid decoder name: %s\n", protocol.c_str());
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
void ProtocolDecoder::SampleOnRisingEdges(DigitalWaveform* data, DigitalWaveform* clock, DigitalWaveform& samples)
{
	samples.clear();

	size_t ndata = 0;
	size_t len = clock->m_offsets.size();
	size_t dlen = data->m_samples.size();
	for(size_t i=1; i<len; i++)
	{
		//Throw away clock samples until we find a rising edge
		if(!(clock->m_samples[i] && !clock->m_samples[i-1]))
			continue;

		//Throw away data samples until the data is synced with us
		int64_t clkstart = clock->m_offsets[i] * clock->m_timescale;
		while( (ndata < dlen) && (data->m_offsets[ndata] * data->m_timescale < clkstart) )
			ndata ++;
		if(ndata >= dlen)
			break;

		//Extend the previous sample's duration (if any) to our start
		size_t ssize = samples.m_samples.size();
		if(ssize)
		{
			size_t last = ssize - 1;
			samples.m_durations[last] = clkstart - samples.m_offsets[last];
		}

		//Add the new sample
		samples.m_offsets.push_back(clkstart);
		samples.m_durations.push_back(1);
		samples.m_samples.push_back(data->m_samples[ndata]);
	}
}

/**
	@brief Samples a digital bus waveform on the rising edges of a clock

	The sampling rate of the data and clock signals need not be equal or uniform.

	The sampled waveform has a time scale in picoseconds regardless of the incoming waveform's time scale.

	@param data		The data signal to sample
	@param clock	The clock signal to use
	@param samples	Output waveform
 */
void ProtocolDecoder::SampleOnRisingEdges(DigitalBusWaveform* data, DigitalWaveform* clock, DigitalBusWaveform& samples)
{
	samples.clear();

	size_t ndata = 0;
	size_t len = clock->m_offsets.size();
	size_t dlen = data->m_samples.size();
	for(size_t i=1; i<len; i++)
	{
		//Throw away clock samples until we find a rising edge
		if(!(clock->m_samples[i] && !clock->m_samples[i-1]))
			continue;

		//Throw away data samples until the data is synced with us
		int64_t clkstart = clock->m_offsets[i] * clock->m_timescale;
		while( (ndata < dlen) && (data->m_offsets[ndata] * data->m_timescale < clkstart) )
			ndata ++;
		if(ndata >= dlen)
			break;

		//Extend the previous sample's duration (if any) to our start
		size_t ssize = samples.m_samples.size();
		if(ssize)
		{
			size_t last = ssize - 1;
			samples.m_durations[last] = clkstart - samples.m_offsets[last];
		}

		//Add the new sample
		samples.m_offsets.push_back(clkstart);
		samples.m_durations.push_back(1);
		samples.m_samples.push_back(data->m_samples[ndata]);
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
void ProtocolDecoder::SampleOnFallingEdges(DigitalWaveform* data, DigitalWaveform* clock, DigitalWaveform& samples)
{
	samples.clear();

	size_t ndata = 0;
	size_t len = clock->m_offsets.size();
	size_t dlen = data->m_samples.size();
	for(size_t i=1; i<len; i++)
	{
		//Throw away clock samples until we find a falling edge
		if(!(!clock->m_samples[i] && clock->m_samples[i-1]))
			continue;

		//Throw away data samples until the data is synced with us
		int64_t clkstart = clock->m_offsets[i] * clock->m_timescale;
		while( (ndata < dlen) && (data->m_offsets[ndata] * data->m_timescale < clkstart) )
			ndata ++;
		if(ndata >= dlen)
			break;

		//Extend the previous sample's duration (if any) to our start
		size_t ssize = samples.m_samples.size();
		if(ssize)
		{
			size_t last = ssize - 1;
			samples.m_durations[last] = clkstart - samples.m_offsets[last];
		}

		//Add the new sample
		samples.m_offsets.push_back(clkstart);
		samples.m_durations.push_back(1);
		samples.m_samples.push_back(data->m_samples[ndata]);
	}
}

/**
	@brief Samples a digital waveform on all edges of a clock

	The sampling rate of the data and clock signals need not be equal or uniform.

	The sampled waveform has a time scale in picoseconds regardless of the incoming waveform's time scale.

	@param data		The data signal to sample
	@param clock	The clock signal to use
	@param samples	Output waveform
 */
void ProtocolDecoder::SampleOnAnyEdges(DigitalWaveform* data, DigitalWaveform* clock, DigitalWaveform& samples)
{
	samples.clear();

	size_t ndata = 0;
	size_t len = clock->m_offsets.size();
	size_t dlen = data->m_samples.size();
	for(size_t i=1; i<len; i++)
	{
		//Throw away clock samples until we find an edge
		if(clock->m_samples[i] == clock->m_samples[i-1])
			continue;

		//Throw away data samples until the data is synced with us
		int64_t clkstart = clock->m_offsets[i] * clock->m_timescale;
		while( (ndata < dlen) && (data->m_offsets[ndata] * data->m_timescale < clkstart) )
			ndata ++;
		if(ndata >= dlen)
			break;

		//Extend the previous sample's duration (if any) to our start
		size_t ssize = samples.m_samples.size();
		if(ssize)
		{
			size_t last = ssize - 1;
			samples.m_durations[last] = clkstart - samples.m_offsets[last];
		}

		//Add the new sample
		samples.m_offsets.push_back(clkstart);
		samples.m_durations.push_back(1);
		samples.m_samples.push_back(data->m_samples[ndata]);
	}
}

/**
	@brief Samples a digital waveform on all edges of a clock

	The sampling rate of the data and clock signals need not be equal or uniform.

	The sampled waveform has a time scale in picoseconds regardless of the incoming waveform's time scale.

	@param data		The data signal to sample
	@param clock	The clock signal to use
	@param samples	Output waveform
 */
void ProtocolDecoder::SampleOnAnyEdges(DigitalBusWaveform* data, DigitalWaveform* clock, DigitalBusWaveform& samples)
{
	samples.clear();

	size_t ndata = 0;
	size_t len = clock->m_offsets.size();
	size_t dlen = data->m_samples.size();
	for(size_t i=1; i<len; i++)
	{
		//Throw away clock samples until we find an edge
		if(clock->m_samples[i] == clock->m_samples[i-1])
			continue;

		//Throw away data samples until the data is synced with us
		int64_t clkstart = clock->m_offsets[i] * clock->m_timescale;
		while( (ndata < dlen) && (data->m_offsets[ndata] * data->m_timescale < clkstart) )
			ndata ++;
		if(ndata >= dlen)
			break;

		//Extend the previous sample's duration (if any) to our start
		size_t ssize = samples.m_samples.size();
		if(ssize)
		{
			size_t last = ssize - 1;
			samples.m_durations[last] = clkstart - samples.m_offsets[last];
		}

		//Add the new sample
		samples.m_offsets.push_back(clkstart);
		samples.m_durations.push_back(1);
		samples.m_samples.push_back(data->m_samples[ndata]);
	}
}

/**
	@brief Find zero crossings in a waveform, interpolating as necessary
 */
void ProtocolDecoder::FindZeroCrossings(AnalogWaveform* data, float threshold, std::vector<int64_t>& edges)
{
	//Find times of the zero crossings (TODO: extract this into reusable function)
	bool first = true;
	bool last = false;
	int64_t phoff = data->m_timescale/2 + data->m_triggerPhase;
	size_t len = data->m_samples.size();
	for(size_t i=1; i<len; i++)
	{
		bool value = data->m_samples[i] > threshold;

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

		//Midpoint of the sample, plus the zero crossing
		int64_t t = phoff + data->m_timescale * (data->m_offsets[i] + InterpolateTime(data, i-1, threshold));
		edges.push_back(t);
		last = value;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

void ProtocolDecoder::LoadParameters(const YAML::Node& node, IDTable& /*table*/)
{
	//id, protocol, color are already loaded
	m_displayname = node["nick"].as<string>();
	m_hwname = node["name"].as<string>();

	auto parameters = node["parameters"];
	for(auto it : parameters)
		GetParameter(it.first.as<string>()).ParseString(it.second.as<string>());
}

void ProtocolDecoder::LoadInputs(const YAML::Node& node, IDTable& table)
{
	auto inputs = node["inputs"];
	for(auto it : inputs)
		SetInput(it.first.as<string>(),	static_cast<OscilloscopeChannel*>(table[it.second.as<int>()]) );
}

string ProtocolDecoder::SerializeConfiguration(IDTable& table)
{
	//Save basic decode info
	char tmp[1024];
	snprintf(tmp, sizeof(tmp), "    : \n");
	string config = tmp;
	snprintf(tmp, sizeof(tmp), "        id:              %d\n", table.emplace(this));
	config += tmp;

	//Channel info
	snprintf(tmp, sizeof(tmp), "        protocol:        \"%s\"\n", GetProtocolDisplayName().c_str());
	config += tmp;
	snprintf(tmp, sizeof(tmp), "        color:           \"%s\"\n", m_displaycolor.c_str());
	config += tmp;
	snprintf(tmp, sizeof(tmp), "        nick:            \"%s\"\n", m_displayname.c_str());
	config += tmp;
	snprintf(tmp, sizeof(tmp), "        name:            \"%s\"\n", GetHwname().c_str());
	config += tmp;

	//Inputs
	snprintf(tmp, sizeof(tmp), "        inputs: \n");
	config += tmp;
	for(size_t i=0; i<m_channels.size(); i++)
	{
		auto chan = m_channels[i];
		if(chan == NULL)
			snprintf(tmp, sizeof(tmp), "            %-20s 0\n", (m_signalNames[i] + ":").c_str());
		else
			snprintf(tmp, sizeof(tmp), "            %-20s %d\n", (m_signalNames[i] + ":").c_str(), table.emplace(chan));
		config += tmp;
	}

	//Parameters
	snprintf(tmp, sizeof(tmp), "        parameters: \n");
	config += tmp;
	for(auto it : m_parameters)
	{
		snprintf(tmp, sizeof(tmp), "            %-20s %s\n", (it.first+":").c_str(), it.second.ToString().c_str());
		config += tmp;
	}

	return config;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Complex protocol decodes

Gdk::Color ProtocolDecoder::GetColor(int /*i*/)
{
	return m_standardColors[COLOR_ERROR];
}

string ProtocolDecoder::GetText(int /*i*/)
{
	return "(unimplemented)";
}

string ProtocolDecoder::GetTextForAsciiChannel(int i)
{
	AsciiWaveform* capture = dynamic_cast<AsciiWaveform*>(GetData());
	if(capture != NULL)
	{
		char c = capture->m_samples[i];
		char sbuf[16] = {0};
		if(isprint(c))
			sbuf[0] = c;
		else if(c == '\r')		//special case common non-printable chars
			return "\\r";
		else if(c == '\n')
			return "\\n";
		else if(c == '\b')
			return "\\b";
		else
			snprintf(sbuf, sizeof(sbuf), "\\x%02x", 0xFF & c);
		return sbuf;
	}
	return "";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Interpolation helpers

/**
	@brief Interpolates the actual time of a threshold crossing between two samples

	Simple linear interpolation for now (TODO sinc)

	@return Interpolated crossing time. 0=a, 1=a+1, fractional values are in between.
 */
float ProtocolDecoder::InterpolateTime(AnalogWaveform* cap, size_t a, float voltage)
{
	//If the voltage isn't between the two points, abort
	float fa = cap->m_samples[a];
	float fb = cap->m_samples[a+1];
	bool ag = (fa > voltage);
	bool bg = (fb > voltage);
	if( (ag && bg) || (!ag && !bg) )
		return 0;

	//no need to divide by time, sample spacing is normalized to 1 timebase unit
	float slope = (fb - fa);
	float delta = voltage - fa;
	return delta / slope;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Measurement helpers

/**
	@brief Gets the lowest voltage of a waveform
 */
float ProtocolDecoder::GetMinVoltage(AnalogWaveform* cap)
{
	//Loop over samples and find the minimum
	float tmp = FLT_MAX;
	for(float f : cap->m_samples)
	{
		if(f < tmp)
			tmp = f;
	}
	return tmp;
}

/**
	@brief Gets the highest voltage of a waveform
 */
float ProtocolDecoder::GetMaxVoltage(AnalogWaveform* cap)
{
	//Loop over samples and find the maximum
	float tmp = -FLT_MAX;
	for(float f : cap->m_samples)
	{
		if(f > tmp)
			tmp = f;
	}
	return tmp;
}

/**
	@brief Gets the average voltage of a waveform
 */
float ProtocolDecoder::GetAvgVoltage(AnalogWaveform* cap)
{
	//Loop over samples and find the average
	//TODO: more numerically stable summation algorithm for deep captures
	double sum = 0;
	for(float f : cap->m_samples)
		sum += f;
	return sum / cap->m_samples.size();
}

/**
	@brief Makes a histogram from a waveform with the specified number of bins.

	Any values outside the range are clamped (put in bin 0 or bins-1 as appropriate).

	@param low	Low endpoint of the histogram (volts)
	@param high High endpoint of the histogram (volts)
	@param bins	Number of histogram bins
 */
vector<size_t> ProtocolDecoder::MakeHistogram(AnalogWaveform* cap, float low, float high, size_t bins)
{
	vector<size_t> ret;
	for(size_t i=0; i<bins; i++)
		ret.push_back(0);

	float delta = high-low;

	for(float v : cap->m_samples)
	{
		float fbin = (v-low) / delta;
		size_t bin = floor(fbin * bins);
		if(fbin < 0)
			bin = 0;
		if(bin >= bins)
			bin = bin-1;
		ret[bin] ++;
	}

	return ret;
}

/**
	@brief Gets the most probable "0" level for a digital waveform
 */
float ProtocolDecoder::GetBaseVoltage(AnalogWaveform* cap)
{
	float vmin = GetMinVoltage(cap);
	float vmax = GetMaxVoltage(cap);
	float delta = vmax - vmin;
	const int nbins = 100;
	auto hist = MakeHistogram(cap, vmin, vmax, nbins);

	//Find the highest peak in the first quarter of the histogram
	size_t binval = 0;
	int idx = 0;
	for(int i=0; i<(nbins/4); i++)
	{
		if(hist[i] > binval)
		{
			binval = hist[i];
			idx = i;
		}
	}

	float fbin = (idx + 0.5f)/nbins;
	return fbin*delta + vmin;
}

/**
	@brief Gets the most probable "1" level for a digital waveform
 */
float ProtocolDecoder::GetTopVoltage(AnalogWaveform* cap)
{
	float vmin = GetMinVoltage(cap);
	float vmax = GetMaxVoltage(cap);
	float delta = vmax - vmin;
	const int nbins = 100;
	auto hist = MakeHistogram(cap, vmin, vmax, nbins);

	//Find the highest peak in the third quarter of the histogram
	size_t binval = 0;
	int idx = 0;
	for(int i=(nbins*3)/4; i<nbins; i++)
	{
		if(hist[i] > binval)
		{
			binval = hist[i];
			idx = i;
		}
	}

	float fbin = (idx + 0.5f)/nbins;
	return fbin*delta + vmin;
}
