/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2021 Andrew D. Zonenberg and contributors                                                         *
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

using namespace std;

Filter::CreateMapType Filter::m_createprocs;
set<Filter*> Filter::m_filters;

mutex Filter::m_cacheMutex;
map<pair<WaveformBase*, float>, vector<int64_t> > Filter::m_zeroCrossingCache;

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
	Category cat,
	const string& kernelPath,
	const string& kernelName)
	: OscilloscopeChannel(NULL, "", type, color, 1)	//TODO: handle this better?
	, m_category(cat)
	, m_dirty(true)
	, m_usingDefault(true)
{
	m_physical = false;
	m_filters.emplace(this);

	//Load our OpenCL kernel, if we have one
	#ifdef HAVE_OPENCL

		m_kernel = NULL;
		m_program = NULL;

		//Important to check g_clContext - OpenCL enabled at compile time does not guarantee that we have any
		//usable OpenCL devices actually present on the system. We might also have disabled it via --noopencl.
		if(kernelPath != "" && g_clContext)
		{
			try
			{
				string kernelSource = ReadDataFile(kernelPath);
				cl::Program::Sources sources(1, make_pair(&kernelSource[0], kernelSource.length()));
				m_program = new cl::Program(*g_clContext, sources);
				m_program->build(g_contextDevices);
				m_kernel = new cl::Kernel(*m_program, kernelName.c_str());
			}
			catch(const cl::Error& e)
			{
				LogError("OpenCL error: %s (%d)\n", e.what(), e.err() );

				if(e.err() == CL_BUILD_PROGRAM_FAILURE)
				{
					LogError("Failed to build OpenCL program from %s\n", kernelPath.c_str());
					string log;
					m_program->getBuildInfo<string>(g_contextDevices[0], CL_PROGRAM_BUILD_LOG, &log);
					LogDebug("Build log:\n");
					LogDebug("%s\n", log.c_str());
				}

				delete m_program;
				delete m_kernel;
				m_program = NULL;
				m_kernel = NULL;
				return;
			}

		}

	#endif
}

Filter::~Filter()
{
	#ifdef HAVE_OPENCL
		delete m_kernel;
		delete m_program;
		m_kernel = NULL;
		m_program = NULL;
	#endif

	m_filters.erase(this);
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

/**
	@brief Returns true if this filter outputs a waveform consisting of a single sample.

	If scalar, the output is displayed with statistics rather than a waveform view.
 */
bool Filter::IsScalarOutput()
{
	return false;
}

bool Filter::UsesCLFFT()
{
	return false;
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

	The sampled waveform has a time scale in femtoseconds regardless of the incoming waveform's time scale.

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
		int64_t clkstart = clock->m_offsets[i] * clock->m_timescale + clock->m_triggerPhase;
		while( (ndata+1 < dlen) && ((data->m_offsets[ndata+1] * data->m_timescale + data->m_triggerPhase) < clkstart) )
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

	The sampled waveform has a time scale in femtoseconds regardless of the incoming waveform's time scale.

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
		int64_t clkstart = clock->m_offsets[i] * clock->m_timescale + clock->m_triggerPhase;
		while( (ndata+1 < dlen) && ((data->m_offsets[ndata+1] * data->m_timescale + data->m_triggerPhase) < clkstart) )
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

	The sampled waveform has a time scale in femtoseconds regardless of the incoming waveform's time scale.

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
		int64_t clkstart = clock->m_offsets[i] * clock->m_timescale + clock->m_triggerPhase;
		while( (ndata+1 < dlen) && ((data->m_offsets[ndata+1] * data->m_timescale + data->m_triggerPhase) < clkstart) )
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

	The sampled waveform has a time scale in femtoseconds regardless of the incoming waveform's time scale.

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
		int64_t clkstart = clock->m_offsets[i] * clock->m_timescale + clock->m_triggerPhase;
		while( (ndata+1 < dlen) && ((data->m_offsets[ndata+1] * data->m_timescale + data->m_triggerPhase) < clkstart) )
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

	The sampled waveform has a time scale in femtoseconds regardless of the incoming waveform's time scale.

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
		int64_t clkstart = clock->m_offsets[i] * clock->m_timescale + clock->m_triggerPhase;
		while( (ndata+1 < dlen) && ((data->m_offsets[ndata+1] * data->m_timescale + data->m_triggerPhase) < clkstart) )
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
void Filter::FindZeroCrossings(AnalogWaveform* data, float threshold, vector<int64_t>& edges)
{
	pair<WaveformBase*, float> cachekey(data, threshold);

	//Check cache
	{
		lock_guard<mutex> lock(m_cacheMutex);
		auto it = m_zeroCrossingCache.find(cachekey);
		if(it != m_zeroCrossingCache.end())
		{
			edges = it->second;
			return;
		}
	}

	//Find times of the zero crossings
	bool first = true;
	bool last = false;
	int64_t phoff = data->m_triggerPhase;
	size_t len = data->m_samples.size();
	float fscale = data->m_timescale;

	if(data->m_densePacked)
	{
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
			int64_t tfrac = fscale * InterpolateTime(data, i-1, threshold);
			int64_t t = phoff + data->m_timescale*(i-1) + tfrac;
			edges.push_back(t);
			last = value;
		}
	}
	else
	{
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
			int64_t tfrac = fscale * InterpolateTime(data, i-1, threshold);
			int64_t t = phoff + data->m_timescale * data->m_offsets[i-1] + tfrac;
			edges.push_back(t);
			last = value;
		}
	}

	//Add to cache
	lock_guard<mutex> lock(m_cacheMutex);
	m_zeroCrossingCache[cachekey] = edges;
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

string Filter::SerializeConfiguration(IDTable& table, size_t /*indent*/)
{
	string config = "    : \n";
	config += FlowGraphNode::SerializeConfiguration(table, 8);

	//Channel info
	char tmp[1024];
	snprintf(tmp, sizeof(tmp), "        protocol:        \"%s\"\n", GetProtocolDisplayName().c_str());
	config += tmp;
	snprintf(tmp, sizeof(tmp), "        color:           \"%s\"\n", m_displaycolor.c_str());
	config += tmp;
	snprintf(tmp, sizeof(tmp), "        nick:            \"%s\"\n", m_displayname.c_str());
	config += tmp;
	snprintf(tmp, sizeof(tmp), "        name:            \"%s\"\n", GetHwname().c_str());
	config += tmp;

	return config;
}

void Filter::LoadParameters(const YAML::Node& node, IDTable& table)
{
	FlowGraphNode::LoadParameters(node, table);

	//id, protocol, color are already loaded
	m_displayname = node["nick"].as<string>();
	m_hwname = node["name"].as<string>();
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

	//Early out if we have zero span
	if(bins == 0)
		return ret;

	float delta = high-low;

	for(float v : cap->m_samples)
	{
		float fbin = (v-low) / delta;
		size_t bin = floor(fbin * bins);
		bin = max(bin, (size_t)0);
		bin = min(bin, bins-1);
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

void Filter::ClearAnalysisCache()
{
	lock_guard<mutex> lock(m_cacheMutex);
	m_zeroCrossingCache.clear();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helpers for various common boilerplate operations

/**
	@brief Sets up an analog output waveform and copies timebase configuration from the input.

	A new output waveform is created if necessary, but when possible the existing one is reused.
	Timestamps are copied from the input to the output.

	@param din			Input waveform
	@param stream		Stream index
	@param skipstart	Number of input samples to discard from the beginning of the waveform
	@param skipend		Number of input samples to discard from the end of the waveform

	@return	The ready-to-use output waveform
 */
AnalogWaveform* Filter::SetupOutputWaveform(WaveformBase* din, size_t stream, size_t skipstart, size_t skipend)
{
	//Create the waveform, but only if necessary
	AnalogWaveform* cap = dynamic_cast<AnalogWaveform*>(GetData(stream));
	if(cap == NULL)
	{
		cap = new AnalogWaveform;
		SetData(cap, stream);
	}

	//Copy configuration
	cap->m_timescale 			= din->m_timescale;
	cap->m_startTimestamp 		= din->m_startTimestamp;
	cap->m_startFemtoseconds	= din->m_startFemtoseconds;
	cap->m_triggerPhase			= din->m_triggerPhase;

	size_t len = din->m_offsets.size() - (skipstart + skipend);
	size_t curlen = cap->m_offsets.size();

	cap->Resize(len);

	//If the input waveform is NOT dense packed, no optimizations possible.
	if(!din->m_densePacked)
	{
		memcpy(&cap->m_offsets[0], &din->m_offsets[skipstart], len*sizeof(int64_t));
		memcpy(&cap->m_durations[0], &din->m_durations[skipstart], len*sizeof(int64_t));
		cap->m_densePacked = false;
	}

	//Input waveform is dense packed, but output is not.
	//Need to clear some old stuff but we can produce a dense packed output.
	//Note that we copy from zero regardless of skipstart to produce a dense packed output.
	//TODO: AVX2 optimizations here so we don't need to read data we already know the value of
	else if(!cap->m_densePacked)
	{
		memcpy(&cap->m_offsets[0], &din->m_offsets[0], len*sizeof(int64_t));
		memcpy(&cap->m_durations[0], &din->m_durations[0], len*sizeof(int64_t));
		cap->m_densePacked = true;
	}

	//Both waveforms are dense packed, but new size is bigger. Need to copy the additional data.
	else if(len > curlen)
	{
		size_t increase = len - curlen;
		memcpy(&cap->m_offsets[curlen], &din->m_offsets[curlen], increase*sizeof(int64_t));
		memcpy(&cap->m_durations[curlen], &din->m_durations[curlen], increase*sizeof(int64_t));
	}

	//Both waveforms are dense packed, new size is smaller or the same.
	//This is what we want: no work needed at all!
	else
	{
	}

	return cap;
}

/**
	@brief Sets up a digital output waveform and copies timebase configuration from the input.

	A new output waveform is created if necessary, but when possible the existing one is reused.
	Timestamps are copied from the input to the output.

	@param din			Input waveform
	@param stream		Stream index
	@param skipstart	Number of input samples to discard from the beginning of the waveform
	@param skipend		Number of input samples to discard from the end of the waveform

	@return	The ready-to-use output waveform
 */
DigitalWaveform* Filter::SetupDigitalOutputWaveform(WaveformBase* din, size_t stream, size_t skipstart, size_t skipend)
{
	//Create the waveform, but only if necessary
	DigitalWaveform* cap = dynamic_cast<DigitalWaveform*>(GetData(stream));
	if(cap == NULL)
	{
		cap = new DigitalWaveform;
		SetData(cap, stream);
	}

	//Copy configuration
	cap->m_timescale 			= din->m_timescale;
	cap->m_startTimestamp 		= din->m_startTimestamp;
	cap->m_startFemtoseconds	= din->m_startFemtoseconds;
	cap->m_triggerPhase			= din->m_triggerPhase;

	size_t len = din->m_offsets.size() - (skipstart + skipend);
	size_t curlen = cap->m_offsets.size();

	cap->Resize(len);

	//If the input waveform is NOT dense packed, no optimizations possible.
	if(!din->m_densePacked)
	{
		memcpy(&cap->m_offsets[0], &din->m_offsets[skipstart], len*sizeof(int64_t));
		memcpy(&cap->m_durations[0], &din->m_durations[skipstart], len*sizeof(int64_t));
		cap->m_densePacked = false;
	}

	//Input waveform is dense packed, but output is not.
	//Need to clear some old stuff but we can produce a dense packed output.
	//Note that we copy from zero regardless of skipstart to produce a dense packed output.
	//TODO: AVX2 optimizations here so we don't need to read data we already know the value of
	else if(!cap->m_densePacked)
	{
		memcpy(&cap->m_offsets[0], &din->m_offsets[0], len*sizeof(int64_t));
		memcpy(&cap->m_durations[0], &din->m_durations[0], len*sizeof(int64_t));
		cap->m_densePacked = true;
	}

	//Both waveforms are dense packed, but new size is bigger. Need to copy the additional data.
	else if(len > curlen)
	{
		size_t increase = len - curlen;
		memcpy(&cap->m_offsets[curlen], &din->m_offsets[curlen], increase*sizeof(int64_t));
		memcpy(&cap->m_durations[curlen], &din->m_durations[curlen], increase*sizeof(int64_t));
	}

	//Both waveforms are dense packed, new size is smaller or the same.
	//This is what we want: no work needed at all!
	else
	{
	}

	return cap;
}

/**
	@brief Calculates a CRC32 checksum using the standard Ethernet polynomial
 */
uint32_t Filter::CRC32(vector<uint8_t>& bytes, size_t start, size_t end)
{
	uint32_t poly = 0xedb88320;

	uint32_t crc = 0xffffffff;
	for(size_t n=start; n <= end; n++)
	{
		uint8_t d = bytes[n];
		for(int i=0; i<8; i++)
		{
			bool b = ( crc ^ (d >> i) ) & 1;
			crc >>= 1;
			if(b)
				crc ^= poly;
		}
	}

	return ~(	((crc & 0x000000ff) << 24) |
				((crc & 0x0000ff00) << 8) |
				((crc & 0x00ff0000) >> 8) |
				 (crc >> 24) );
}
