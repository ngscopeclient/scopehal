/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2022 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Implementation of MockOscilloscope
 */

#include "scopehal.h"
#include "OscilloscopeChannel.h"
#include "MockOscilloscope.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

MockOscilloscope::MockOscilloscope(const string& name, const string& vendor, const string& serial)
	: m_name(name)
	, m_vendor(vendor)
	, m_serial(serial)
	, m_extTrigger(NULL)
{
}

MockOscilloscope::~MockOscilloscope()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Information queries

bool MockOscilloscope::IsOffline()
{
	return true;
}

string MockOscilloscope::IDPing()
{
	return "";
}

string MockOscilloscope::GetTransportName()
{
	return "null";
}

string MockOscilloscope::GetTransportConnectionString()
{
	return "";
}

string MockOscilloscope::GetDriverNameInternal()
{
	return "mock";
}

unsigned int MockOscilloscope::GetInstrumentTypes()
{
	return INST_OSCILLOSCOPE;
}

string MockOscilloscope::GetName()
{
	return m_name;
}

string MockOscilloscope::GetVendor()
{
	return m_vendor;
}

string MockOscilloscope::GetSerial()
{
	return m_serial;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering

Oscilloscope::TriggerMode MockOscilloscope::PollTrigger()
{
	//we never trigger
	return TRIGGER_MODE_STOP;
}

bool MockOscilloscope::AcquireData()
{
	//no new data possible
	return false;
}

void MockOscilloscope::ArmTrigger()
{
	//no-op, we never trigger
}

void MockOscilloscope::StartSingleTrigger()
{
	//no-op, we never trigger
}

void MockOscilloscope::Start()
{
	//no-op, we never trigger
}

void MockOscilloscope::Stop()
{
	//no-op, we never trigger
}

void MockOscilloscope::ForceTrigger()
{
	//no-op, we never trigger
}

bool MockOscilloscope::IsTriggerArmed()
{
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

void MockOscilloscope::LoadConfiguration(const YAML::Node& node, IDTable& table)
{
	//Load the channels
	auto& chans = node["channels"];
	for(auto it : chans)
	{
		auto& cnode = it.second;

		//Allocate channel space if we didn't have it yet
		size_t index = cnode["index"].as<int>();
		if(m_channels.size() < (index+1))
			m_channels.resize(index+1);

		//Configure the channel
		OscilloscopeChannel::ChannelType type = OscilloscopeChannel::CHANNEL_TYPE_COMPLEX;
		string stype = cnode["type"].as<string>();
		if(stype == "analog")
			type = OscilloscopeChannel::CHANNEL_TYPE_ANALOG;
		else if(stype == "digital")
			type = OscilloscopeChannel::CHANNEL_TYPE_DIGITAL;
		else if(stype == "trigger")
			type = OscilloscopeChannel::CHANNEL_TYPE_TRIGGER;
		auto chan = new OscilloscopeChannel(
			this,
			cnode["name"].as<string>(),
			type,
			cnode["color"].as<string>(),
			1,
			index,
			true);
		m_channels[index] = chan;

		//Create the channel ID
		table.emplace(cnode["id"].as<int>(), chan);
	}

	//Call the base class to configure everything
	Oscilloscope::LoadConfiguration(node, table);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Channel configuration. Mostly trivial stubs.

bool MockOscilloscope::IsChannelEnabled(size_t i)
{
	return m_channelsEnabled[i];
}

void MockOscilloscope::EnableChannel(size_t i)
{
	m_channelsEnabled[i] = true;
}

void MockOscilloscope::DisableChannel(size_t i)
{
	m_channelsEnabled[i] = false;
}

vector<OscilloscopeChannel::CouplingType> MockOscilloscope::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	ret.push_back(OscilloscopeChannel::COUPLE_DC_1M);
	ret.push_back(OscilloscopeChannel::COUPLE_AC_1M);
	ret.push_back(OscilloscopeChannel::COUPLE_DC_50);
	ret.push_back(OscilloscopeChannel::COUPLE_GND);
	//TODO: other options? or none?
	return ret;
}

OscilloscopeChannel::CouplingType MockOscilloscope::GetChannelCoupling(size_t i)
{
	return m_channelCoupling[i];
}

void MockOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	m_channelCoupling[i] = type;
}

double MockOscilloscope::GetChannelAttenuation(size_t i)
{
	return m_channelAttenuation[i];
}

void MockOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	m_channelAttenuation[i] = atten;
}

int MockOscilloscope::GetChannelBandwidthLimit(size_t i)
{
	return m_channelBandwidth[i];
}

void MockOscilloscope::SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz)
{
	m_channelBandwidth[i] = limit_mhz;
}

float MockOscilloscope::GetChannelVoltageRange(size_t i, size_t stream)
{
	return m_channelVoltageRange[pair<size_t, size_t>(i, stream)];
}

void MockOscilloscope::SetChannelVoltageRange(size_t i, size_t stream, float range)
{
	m_channelVoltageRange[pair<size_t, size_t>(i, stream)] = range;
}

OscilloscopeChannel* MockOscilloscope::GetExternalTrigger()
{
	return m_extTrigger;
}

float MockOscilloscope::GetChannelOffset(size_t i, size_t stream)
{
	return m_channelOffset[pair<size_t, size_t>(i, stream)];
}

void MockOscilloscope::SetChannelOffset(size_t i, size_t stream, float offset)
{
	m_channelOffset[pair<size_t, size_t>(i, stream)] = offset;
}

vector<uint64_t> MockOscilloscope::GetSampleRatesNonInterleaved()
{
	//no-op
	vector<uint64_t> ret;
	return ret;
}

vector<uint64_t> MockOscilloscope::GetSampleRatesInterleaved()
{
	//no-op
	vector<uint64_t> ret;
	return ret;
}

set<Oscilloscope::InterleaveConflict> MockOscilloscope::GetInterleaveConflicts()
{
	//no-op
	set<Oscilloscope::InterleaveConflict> ret;
	return ret;
}

vector<uint64_t> MockOscilloscope::GetSampleDepthsNonInterleaved()
{
	//no-op
	vector<uint64_t> ret;
	return ret;
}

vector<uint64_t> MockOscilloscope::GetSampleDepthsInterleaved()
{
	//no-op
	vector<uint64_t> ret;
	return ret;
}

uint64_t MockOscilloscope::GetSampleRate()
{
	return 1;
}

uint64_t MockOscilloscope::GetSampleDepth()
{
	//FIXME
	return 1;
}

void MockOscilloscope::SetSampleDepth(uint64_t /*depth*/)
{
	//no-op
}

void MockOscilloscope::SetSampleRate(uint64_t /*rate*/)
{
	//no-op
}

void MockOscilloscope::SetTriggerOffset(int64_t /*offset*/)
{
	//FIXME
}

int64_t MockOscilloscope::GetTriggerOffset()
{
	//FIXME
	return 0;
}

bool MockOscilloscope::IsInterleaving()
{
	return false;
}

bool MockOscilloscope::SetInterleaving(bool /*combine*/)
{
	return false;
}

void MockOscilloscope::PushTrigger()
{
	//no-op
}

void MockOscilloscope::PullTrigger()
{
	//no-op
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Import a waveform from file

/**
	@brief Imports waveforms from Agilent/Keysight/Rigol binary capture files
 */
bool MockOscilloscope::LoadBIN(const string& path)
{
	LogTrace("Importing BIN file \"%s\"\n", path.c_str());
	LogIndenter li_f;

	string f = ReadFile(path);
	uint32_t fpos = 0;

	FileHeader fh;
	f.copy((char*)&fh, sizeof(FileHeader), fpos);
	fpos += sizeof(FileHeader);

	//Get vendor from file signature
	switch(fh.magic[0])
	{
		case 'A':
			m_vendor = "Agilent/Keysight";
			break;

		case 'R':
			m_vendor = "Rigol";
			break;

		default:
			LogError("Unknown file format");
			return false;
	}

	LogDebug("Vendor:    %s\n", m_vendor.c_str());
	//LogDebug("File size: %i bytes\n", fh.length);
	LogDebug("Waveforms: %i\n\n", fh.count);

	//Load waveforms
	for(size_t i=0; i<fh.count; i++)
	{
		LogDebug("Waveform %i:\n", (int)i+1);
		LogIndenter li_w;

		//Parse waveform header
		WaveHeader wh;
		f.copy((char*)&wh, sizeof(WaveHeader), fpos);
		fpos += sizeof(WaveHeader);

		// Only set name/serial on first waveform
		if (i == 0)
		{
			// Split hardware string
			int idx = 0;
			for(int c=0; c<24; c++)
			{
				if(wh.hardware[c] == ':')
				{
					idx = c;
					break;
				}
			}

			//Set oscilloscope metadata
			m_name.assign(wh.hardware, idx);
			m_serial.assign(wh.hardware + idx + 1, 24 - idx);
		}

		LogDebug("Samples:      %i\n", wh.samples);
		LogDebug("Buffers:      %i\n", wh.buffers);
		LogDebug("Type:         %i\n", wh.type);
		LogDebug("Duration:     %.*f us\n", 2, wh.duration * 1e6);
		LogDebug("Start:        %.*f us\n", 2, wh.start * 1e6);
		LogDebug("Interval:     %.*f ns\n", 2, wh.interval * 1e9);
		LogDebug("Origin:       %.*f us\n", 2, wh.origin * 1e6);
		LogDebug("Holdoff:      %.*f ms\n", 2, wh.holdoff * 1e3);
		LogDebug("Sample Rate:  %.*f Msps\n", 2, (1 / wh.interval) / 1e6);
		LogDebug("Frame:        %s\n", m_name.c_str());
		LogDebug("Serial:       %s\n\n", m_serial.c_str());

		// Create new channel
		auto chan = new OscilloscopeChannel(
			this,			//Parent scope
			wh.label,		//Channel name
			OscilloscopeChannel::CHANNEL_TYPE_ANALOG,
			GetDefaultChannelColor(i),
			units[wh.x],
			units[wh.y],
			1,				//Bus width
			i,				//Channel index
			true			//Is physical channel
		);
		AddChannel(chan);
		chan->SetDefaultDisplayName();

		//Create new waveform for channel
		auto wfm = new AnalogWaveform;
		wfm->m_timescale = wh.interval * 1e15;
		wfm->m_startTimestamp = 0;
		wfm->m_startFemtoseconds = 0;
		wfm->m_triggerPhase = 0;
		chan->SetData(wfm, 0);

		//Loop through waveform buffers
		float vmin = FLT_MAX;
		float vmax = -FLT_MAX;
		for(size_t j=0; j<wh.buffers; j++)
		{
			LogDebug("Buffer %i:\n", (int)j+1);
			LogIndenter li_b;

			//Parse waveform data header
			DataHeader dh;
			f.copy((char*)&dh, sizeof(DataHeader), fpos);
			fpos += sizeof(DataHeader);

    		LogDebug("Data Type:      %i\n", dh.type);
    		LogDebug("Sample depth:   %i bits\n", dh.depth*8);
    		LogDebug("Buffer length:  %i KB\n\n\n", dh.length/1024);

			//Loop through waveform samples
			float sample = 0;
			if (dh.type == 6)
			{
				//Integer samples (digital waveforms)
				uint8_t* sample_i = nullptr;
				for(size_t k=0; k<wh.samples; k++)
				{
					sample_i = (uint8_t*)(f.c_str() + fpos);
					sample = (float)*sample_i;

					//Push sample to waveform
					wfm->m_offsets.push_back(k);
					wfm->m_samples.push_back(sample);
					wfm->m_durations.push_back(1);

					//Update voltage min/max values
					vmax = max(vmax, sample);
					vmin = min(vmin, sample);

					fpos += dh.depth;
				}
			}
			else
			{
				//Float samples (analog waveforms)
				float* sample_f = nullptr;
				for(size_t k=0; k<wh.samples; k++)
				{
					sample_f = (float*)(f.c_str() + fpos);
					sample = *sample_f;

					//Push sample to waveform
					wfm->m_offsets.push_back(k);
					wfm->m_samples.push_back(sample);
					wfm->m_durations.push_back(1);

					//Update voltage min/max values
					vmax = max(vmax, sample);
					vmin = min(vmin, sample);

					fpos += dh.depth;
				}
			}
		}

		//Calculate offset and range
		chan->SetVoltageRange((vmax-vmin) * 1.5, 0);
		chan->SetOffset(-((vmax-abs(vmin)) / 2), 0);
	}

	return true;
}

/**
	@brief Imports a waveform from a VCD file
 */
bool MockOscilloscope::LoadVCD(const string& path)
{
	FILE* fp = fopen(path.c_str(), "r");
	if(!fp)
	{
		LogError("Couldn't open VCD file \"%s\"\n", path.c_str());
		return false;
	}

	enum
	{
		STATE_IDLE,
		STATE_DATE,
		STATE_VERSION,
		STATE_TIMESCALE,
		STATE_VARS,
		STATE_INITIAL,
		STATE_DUMP
	} state = STATE_IDLE;

	time_t timestamp = 0;
	int64_t fs = 0;
	int64_t timescale = 1;

	int64_t current_time = 0;

	//Current scope prefix for signals
	vector<string> scope;

	//Map of signal IDs to signals
	map<string, WaveformBase*> waveforms;
	map<string, size_t> widths;

	//VCD is a line based format, so process everything in lines
	char buf[2048];
	while(NULL != fgets(buf, sizeof(buf), fp))
	{
		string s = Trim(buf);

		//Changing time is always legal, even before we get to the main variable dumping section.
		//(Xilinx Vivado-generated VCDs include a #0 before the $dumpvars section.)
		if(s[0] == '#')
		{
			current_time = stoll(buf+1);
			continue;
		}

		//Scope is a bit special since it can nest. Handle that separately.
		else if(s.find("$scope") != string::npos)
		{
			//Get the actual scope
			char name[128];
			if(1 == sscanf(buf, "$scope module %127s", name))
				scope.push_back(name);
			state = STATE_VARS;
			continue;
		}

		//Main state machine
		switch(state)
		{
			case STATE_IDLE:
				if(s == "$date")
					state = STATE_DATE;
				else if(s == "$version")
					state = STATE_VERSION;
				else if(s == "$timescale")
					state = STATE_TIMESCALE;
				else if(s == "$dumpvars")
					state = STATE_INITIAL;
				else
					LogWarning("Don't know what to do with line %s\n", s.c_str());
				break;	//end STATE_IDLE

			case STATE_DATE:
				if(s[0] != '$')
				{
					tm now;
					time_t tnow;
					time(&tnow);
					localtime_r(&tnow, &now);

					tm stamp;

					//Read the date
					//Assume it's formatted "Fri May 21 07:16:38 2021" for now
					char dow[16];
					char month[16];
					if(7 == sscanf(
						buf,
						"%3s %3s %d %d:%d:%d %d",
						dow, month, &stamp.tm_mday, &stamp.tm_hour, &stamp.tm_min, &stamp.tm_sec, &stamp.tm_year))
					{
						string sm(month);
						if(sm == "Jan")
							stamp.tm_mon = 0;
						else if(sm == "Feb")
							stamp.tm_mon = 1;
						else if(sm == "Mar")
							stamp.tm_mon = 2;
						else if(sm == "Apr")
							stamp.tm_mon = 3;
						else if(sm == "May")
							stamp.tm_mon = 4;
						else if(sm == "Jun")
							stamp.tm_mon = 5;
						else if(sm == "Jul")
							stamp.tm_mon = 6;
						else if(sm == "Aug")
							stamp.tm_mon = 7;
						else if(sm == "Sep")
							stamp.tm_mon = 8;
						else if(sm == "Oct")
							stamp.tm_mon = 9;
						else if(sm == "Nov")
							stamp.tm_mon = 10;
						else
							stamp.tm_mon = 11;

						//tm_year isn't absolute year, it's offset from 1900
						stamp.tm_year -= 1900;

						//TODO: figure out if this day/month/year was DST or not.
						//For now, assume same as current. This is going to be off by an hour for half the year!
						stamp.tm_isdst = now.tm_isdst;

						//We can finally get the actual time_t
						timestamp = mktime(&stamp);
					}
				}
				break;	//end STATE_DATE;

			case STATE_VERSION:
				//ignore
				break;	//end STATE_VERSION

			case STATE_TIMESCALE:
				if(s[0] != '$')
				{
					Unit ufs(Unit::UNIT_FS);
					timescale = ufs.ParseString(s);
				}
				break;	//end STATE_VERSION

			case STATE_VARS:
				if(s.find("$upscope") != string::npos)
				{
					if(!scope.empty())
						scope.pop_back();
				}
				else if(s.find("$enddefinitions") != string::npos)
					state = STATE_IDLE;
				else
				{
					//Format the current scope
					string sscope;
					for(auto level : scope)
						sscope += level + "/";

					//Parse the line
					char vtype[16];	//"reg" or "wire", ignored
					int width;
					char symbol[16];
					char name[128];
					if(4 != sscanf(buf, "$var %15[^ ] %d %15[^ ] %127[^ ]", vtype, &width, symbol, name))
						continue;

					//If the symbol is already in use, skip it.
					//We don't support one symbol with more than one name for now
					if(waveforms.find(symbol) != waveforms.end())
						continue;

					//Create the channel
					size_t ichan = m_channels.size();
					string vname = sscope + name;
					auto chan = new OscilloscopeChannel(
						this,
						vname,
						OscilloscopeChannel::CHANNEL_TYPE_DIGITAL,
						GetDefaultChannelColor(ichan),
						width,
						ichan,
						true);
					m_channels.push_back(chan);

					//Create the waveform
					WaveformBase* wfm;
					if(width == 1)
						wfm = new DigitalWaveform;
					else
						wfm = new DigitalBusWaveform;

					wfm->m_timescale = timescale;
					wfm->m_startTimestamp = timestamp;
					wfm->m_startFemtoseconds = fs;
					wfm->m_triggerPhase = 0;
					wfm->m_densePacked = false;
					waveforms[symbol] = wfm;
					widths[symbol] = width;
					chan->SetData(wfm, 0);
				}
				break;	//end STATE_VARS

			case STATE_INITIAL:
			case STATE_DUMP:

				//Parse the current line
				if(s[0] != '$')
				{
					//Vector: first char is 'b', then data, space, symbol name
					if(s[0] == 'b')
					{
						auto ispace = s.find(' ');
						auto symbol = s.substr(ispace + 1);
						auto wfm = dynamic_cast<DigitalBusWaveform*>(waveforms[symbol]);
						if(wfm)
						{
							//Parse the sample data (skipping the leading 'b')
							vector<bool> sample;
							for(size_t i = ispace-1; i > 0; i--)
							{
								if(s[i] == '1')
									sample.push_back(true);
								else
									sample.push_back(false);
							}

							//Zero-pad the sample out to full width
							auto width = widths[symbol];
							while(sample.size() < width)
								sample.push_back(false);

							//Extend the previous sample, if there is one
							auto len = wfm->m_samples.size();
							if(len)
							{
								auto last = len-1;
								wfm->m_durations[last] = current_time - wfm->m_offsets[last];
							}

							//Add the new sample
							wfm->m_offsets.push_back(current_time);
							wfm->m_durations.push_back(1);
							wfm->m_samples.push_back(sample);
						}
						else
							LogError("Symbol \"%s\" is not a valid digital bus waveform\n", symbol.c_str());
					}

					//Scalar: first char is boolean value, rest is symbol name
					else
					{
						auto symbol = s.substr(1);
						auto wfm = dynamic_cast<DigitalWaveform*>(waveforms[symbol]);
						if(wfm)
						{
							//Extend the previous sample, if there is one
							auto len = wfm->m_samples.size();
							if(len)
							{
								auto last = len-1;
								wfm->m_durations[last] = current_time - wfm->m_offsets[last];
							}

							//Add the new sample
							wfm->m_offsets.push_back(current_time);
							wfm->m_durations.push_back(1);
							wfm->m_samples.push_back(s[0] == '1');
						}
						else
							LogError("Symbol \"%s\" is not a valid digital waveform\n", symbol.c_str());
					}
				}

				break;	//end STATE_INITIAL / STATE_DUMP
		}

		//Reset at the end of a block
		if(s.find("$end") != string::npos)
		{
			if(state == STATE_INITIAL)
				state = STATE_DUMP;
			else if(state != STATE_VARS)
				state = STATE_IDLE;
		}
	}
	fclose(fp);

	//Nothing to do if we didn't get any channels
	if(m_channels.empty())
		return false;

	//Find the longest common prefix from all signal names
	string prefix = m_channels[0]->GetHwname();
	for(size_t i=1; i<m_channels.size(); i++)
	{
		string name = m_channels[i]->GetHwname();
		size_t nlen = 1;
		for(; (nlen < prefix.length()) && (nlen < name.length()); nlen ++)
		{
			if(name[nlen] != prefix[nlen])
				break;
		}
		prefix.resize(nlen);
	}

	//Remove the prefix from all signal names
	for(auto chan : m_channels)
		chan->SetDisplayName(chan->GetHwname().substr(prefix.length()));

	return true;
}

/**
	@brief Calculate min/max of each channel and adjust gain/offset accordingly
 */
void MockOscilloscope::AutoscaleVertical()
{
	for(auto c : m_channels)
	{
		auto wfm = dynamic_cast<AnalogWaveform*>(c->GetData(0));
		if(!wfm)
			continue;
		if(wfm->m_samples.empty())
			continue;

		float vmin = wfm->m_samples[0];
		float vmax = vmin;

		for(auto s : wfm->m_samples)
		{
			vmin = min(vmin, (float)s);
			vmax = max(vmax, (float)s);
		}

		//Calculate bounds
		c->SetVoltageRange((vmax - vmin) * 1.05, 0);
		c->SetOffset( -( (vmax - vmin)/2 + vmin ), 0);
	}
}
