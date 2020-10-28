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
	@brief Implementation of Filter
 */

#include "scopehal.h"
#include "Filter.h"

Filter::CreateMapType Filter::m_createprocs;
std::set<Filter*> Filter::m_filters;

using namespace std;

Gdk::Color Filter::m_standardColors[STANDARD_COLOR_COUNT] =
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
// Construction / destruction

Filter::Filter(
	OscilloscopeChannel::ChannelType type,
	const string& color,
	Category cat)
	: OscilloscopeChannel(NULL, "", type, color, 1)	//TODO: handle this better?
	, m_category(cat)
	, m_dirty(true)
{
	m_physical = false;
	m_filters.emplace(this);
}

Filter::~Filter()
{
	m_filters.erase(this);

	for(auto c : m_inputs)
	{
		if(c.m_channel != NULL)
			c.m_channel->Release();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void Filter::ClearSweeps()
{
	//default no-op implementation
}

void Filter::AddRef()
{
	m_refcount ++;
}

void Filter::Release()
{
	m_refcount --;
	if(m_refcount == 0)
		delete this;
}

bool Filter::IsOverlay()
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Refreshing

void Filter::RefreshInputsIfDirty()
{
	for(auto c : m_inputs)
	{
		if(!c.m_channel)
			continue;
		auto f = dynamic_cast<Filter*>(c.m_channel);
		if(f)
			f->RefreshIfDirty();
	}
}

void Filter::RefreshIfDirty()
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

void Filter::DoAddDecoderClass(const string& name, CreateProcType proc)
{
	m_createprocs[name] = proc;
}

void Filter::EnumProtocols(vector<string>& names)
{
	for(CreateMapType::iterator it=m_createprocs.begin(); it != m_createprocs.end(); ++it)
		names.push_back(it->first);
}

Filter* Filter::CreateFilter(const string& protocol, const string& color)
{
	if(m_createprocs.find(protocol) != m_createprocs.end())
		return m_createprocs[protocol](color);

	LogError("Invalid filter name: %s\n", protocol.c_str());
	return NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Input verification helpers

/**
	@brief Returns true if a given input to the filter is non-NULL (and, optionally has a non-empty waveform present)
 */
bool Filter::VerifyInputOK(size_t i, bool allowEmpty)
{
	auto p = m_inputs[i];

	if(p.m_channel == NULL)
		return false;
	auto data = p.GetData();
	if(data == NULL)
		return false;

	if(!allowEmpty)
	{
		if(data->m_offsets.size() == 0)
			return false;
	}

	return true;
}

/**
	@brief Returns true if every input to the filter is non-NULL (and, optionally has a non-empty waveform present)
 */
bool Filter::VerifyAllInputsOK(bool allowEmpty)
{
	for(size_t i=0; i<m_inputs.size(); i++)
	{
		if(!VerifyInputOK(i, allowEmpty))
			return false;
	}

	return true;
}

/**
	@brief Returns true if every input to the filter is non-NULL and has a non-empty analog waveform present
 */
bool Filter::VerifyAllInputsOKAndAnalog()
{
	for(auto p : m_inputs)
	{
		if(p.m_channel == NULL)
			return false;

		auto data = p.m_channel->GetData(p.m_stream);
		if(data == NULL)
			return false;
		if(data->m_offsets.size() == 0)
			return false;

		auto adata = dynamic_cast<AnalogWaveform*>(data);
		if(adata == NULL)
			return false;
	}

	return true;
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
void Filter::SampleOnRisingEdges(DigitalWaveform* data, DigitalWaveform* clock, DigitalWaveform& samples)
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
		while( (ndata+1 < dlen) && (data->m_offsets[ndata+1] * data->m_timescale < clkstart) )
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
void Filter::SampleOnRisingEdges(DigitalBusWaveform* data, DigitalWaveform* clock, DigitalBusWaveform& samples)
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
		while( (ndata+1 < dlen) && (data->m_offsets[ndata+1] * data->m_timescale < clkstart) )
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
void Filter::SampleOnFallingEdges(DigitalWaveform* data, DigitalWaveform* clock, DigitalWaveform& samples)
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
		while( (ndata+1 < dlen) && (data->m_offsets[ndata+1] * data->m_timescale < clkstart) )
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
void Filter::SampleOnAnyEdges(DigitalWaveform* data, DigitalWaveform* clock, DigitalWaveform& samples)
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
		while( (ndata+1 < dlen) && (data->m_offsets[ndata+1] * data->m_timescale < clkstart) )
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
void Filter::SampleOnAnyEdges(DigitalBusWaveform* data, DigitalWaveform* clock, DigitalBusWaveform& samples)
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
		while( (ndata+1 < dlen) && (data->m_offsets[ndata+1] * data->m_timescale < clkstart) )
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
void Filter::FindZeroCrossings(AnalogWaveform* data, float threshold, std::vector<int64_t>& edges)
{
	//Find times of the zero crossings
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

/**
	@brief Find zero crossings in a waveform, interpolating as necessary
 */
void Filter::FindZeroCrossings(AnalogWaveform* data, float threshold, std::vector<double>& edges)
{
	//Find times of the zero crossings
	bool first = true;
	bool last = false;
	double phoff = data->m_timescale/2 + data->m_triggerPhase;
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
		double t = phoff + data->m_timescale * (data->m_offsets[i] + InterpolateTime(data, i-1, threshold));
		edges.push_back(t);
		last = value;
	}
}

/**
	@brief Find edges in a waveform, discarding repeated samples
 */
void Filter::FindZeroCrossings(DigitalWaveform* data, vector<int64_t>& edges)
{
	//Find times of the zero crossings
	bool first = true;
	bool last = data->m_samples[0];
	int64_t phoff = data->m_timescale/2 + data->m_triggerPhase;
	size_t len = data->m_samples.size();
	for(size_t i=1; i<len; i++)
	{
		bool value = data->m_samples[i];

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

		edges.push_back(phoff + data->m_timescale * data->m_offsets[i]);
		last = value;
	}
}

/**
	@brief Find rising edges in a waveform
 */
void Filter::FindRisingEdges(DigitalWaveform* data, vector<int64_t>& edges)
{
	//Find times of the zero crossings
	bool first = true;
	bool last = data->m_samples[0];
	int64_t phoff = data->m_timescale/2 + data->m_triggerPhase;
	size_t len = data->m_samples.size();
	for(size_t i=1; i<len; i++)
	{
		bool value = data->m_samples[i];

		//Save the last value
		if(first)
		{
			last = value;
			first = false;
			continue;
		}

		//Save samples with an edge
		if(value && !last)
			edges.push_back(phoff + data->m_timescale * data->m_offsets[i]);

		last = value;
	}
}

/**
	@brief Find falling edges in a waveform
 */
void Filter::FindFallingEdges(DigitalWaveform* data, vector<int64_t>& edges)
{
	//Find times of the zero crossings
	bool first = true;
	bool last = data->m_samples[0];
	int64_t phoff = data->m_timescale/2 + data->m_triggerPhase;
	size_t len = data->m_samples.size();
	for(size_t i=1; i<len; i++)
	{
		bool value = data->m_samples[i];

		//Save the last value
		if(first)
		{
			last = value;
			first = false;
			continue;
		}

		//Save samples with an edge
		if(!value && last)
			edges.push_back(phoff + data->m_timescale * data->m_offsets[i]);

		last = value;
	}
}

/**
	@brief Find edges in a waveform, discarding repeated samples

	No extra resolution vs the int64 version, just for interface compatibility with the analog interpolating version.
 */
void Filter::FindZeroCrossings(DigitalWaveform* data, vector<double>& edges)
{
	vector<int64_t> tmp;
	FindZeroCrossings(data, tmp);
	for(auto e : tmp)
		edges.push_back(e);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

void Filter::LoadParameters(const YAML::Node& node, IDTable& /*table*/)
{
	//id, protocol, color are already loaded
	m_displayname = node["nick"].as<string>();
	m_hwname = node["name"].as<string>();

	auto parameters = node["parameters"];
	for(auto it : parameters)
		GetParameter(it.first.as<string>()).ParseString(it.second.as<string>());
}

void Filter::LoadInputs(const YAML::Node& node, IDTable& table)
{
	int index;
	int stream;

	auto inputs = node["inputs"];
	for(auto it : inputs)
	{
		//Inputs are formatted as %d/%d. Stream index may be omitted.
		auto sin = it.second.as<string>();
		if(2 != sscanf(sin.c_str(), "%d/%d", &index, &stream))
		{
			index = atoi(sin.c_str());
			stream = 0;
		}

		SetInput(
			it.first.as<string>(),
			StreamDescriptor(static_cast<OscilloscopeChannel*>(table[index]), stream),
			true
			);
	}
}

string Filter::SerializeConfiguration(IDTable& table)
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
	for(size_t i=0; i<m_inputs.size(); i++)
	{
		auto desc = m_inputs[i];
		if(desc.m_channel == NULL)
			snprintf(tmp, sizeof(tmp), "            %-20s 0\n", (m_signalNames[i] + ":").c_str());
		else
		{
			snprintf(tmp, sizeof(tmp), "            %-20s %d/%zu\n",
				(m_signalNames[i] + ":").c_str(),
				table.emplace(desc.m_channel),
				desc.m_stream
			);
		}
		config += tmp;
	}

	//Parameters
	snprintf(tmp, sizeof(tmp), "        parameters: \n");
	config += tmp;
	for(auto it : m_parameters)
	{
		switch(it.second.GetType())
		{
			case FilterParameter::TYPE_FLOAT:
			case FilterParameter::TYPE_INT:
			case FilterParameter::TYPE_BOOL:
				snprintf(
					tmp,
					sizeof(tmp),
					"            %-20s %s\n", (it.first+":").c_str(), it.second.ToString().c_str());
				break;

			case FilterParameter::TYPE_FILENAME:
			case FilterParameter::TYPE_FILENAMES:
			default:
				snprintf(
					tmp,
					sizeof(tmp),
					"            %-20s \"%s\"\n", (it.first+":").c_str(), it.second.ToString().c_str());
				break;
		}

		config += tmp;
	}

	return config;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Complex protocol decodes

Gdk::Color Filter::GetColor(int /*i*/)
{
	return m_standardColors[COLOR_ERROR];
}

string Filter::GetText(int /*i*/)
{
	return "(unimplemented)";
}

string Filter::GetTextForAsciiChannel(int i, size_t stream)
{
	AsciiWaveform* capture = dynamic_cast<AsciiWaveform*>(GetData(stream));
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
float Filter::InterpolateTime(AnalogWaveform* cap, size_t a, float voltage)
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

/**
	@brief Interpolates the actual time of a differential threshold crossing between two samples

	Simple linear interpolation for now (TODO sinc)

	@return Interpolated crossing time. 0=a, 1=a+1, fractional values are in between.
 */
float Filter::InterpolateTime(AnalogWaveform* p, AnalogWaveform* n, size_t a, float voltage)
{
	//If the voltage isn't between the two points, abort
	float fa = p->m_samples[a] - n->m_samples[a];
	float fb = p->m_samples[a+1] - n->m_samples[a+1];
	bool ag = (fa > voltage);
	bool bg = (fb > voltage);
	if( (ag && bg) || (!ag && !bg) )
		return 0;

	//no need to divide by time, sample spacing is normalized to 1 timebase unit
	float slope = (fb - fa);
	float delta = voltage - fa;
	return delta / slope;
}

/**
	@brief Interpolates the actual value of a point between two samples

	@param cap			Waveform to work with
	@param index		Starting position
	@param frac_ticks	Fractional position of the sample.
						Note that this is in timebase ticks, so if some samples are >1 tick apart it's possible for
						this value to be outside [0, 1].
 */
float Filter::InterpolateValue(AnalogWaveform* cap, size_t index, float frac_ticks)
{
	float frac = frac_ticks / (cap->m_offsets[index+1] - cap->m_offsets[index]);
	float v1 = cap->m_samples[index];
	float v2 = cap->m_samples[index+1];
	return v1 + (v2-v1)*frac;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Measurement helpers

/**
	@brief Gets the lowest voltage of a waveform
 */
float Filter::GetMinVoltage(AnalogWaveform* cap)
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
float Filter::GetMaxVoltage(AnalogWaveform* cap)
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
float Filter::GetAvgVoltage(AnalogWaveform* cap)
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
vector<size_t> Filter::MakeHistogram(AnalogWaveform* cap, float low, float high, size_t bins)
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
float Filter::GetBaseVoltage(AnalogWaveform* cap)
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
float Filter::GetTopVoltage(AnalogWaveform* cap)
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
