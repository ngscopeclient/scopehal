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

double MockOscilloscope::GetChannelVoltageRange(size_t i)
{
	return m_channelVoltageRange[i];
}

void MockOscilloscope::SetChannelVoltageRange(size_t i, double range)
{
	m_channelVoltageRange[i] = range;
}

OscilloscopeChannel* MockOscilloscope::GetExternalTrigger()
{
	return m_extTrigger;
}

double MockOscilloscope::GetChannelOffset(size_t i)
{
	return m_channelOffset[i];
}

void MockOscilloscope::SetChannelOffset(size_t i, double offset)
{
	m_channelOffset[i] = offset;
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
	@brief Reads bytes from file into array and returns the array pointer
 */
char* MockOscilloscope::ReadFromFile(int len, FILE* fp)
{
	char* buf = new char[len + 1]();
	fread(buf, 1, len, fp);

	return buf;
}

/**
	@brief Converts char array to integer
 */
int MockOscilloscope::BytesToInt(char* b, int len)
{
	int i = (uint8_t)b[0] | ((uint8_t)b[1] << 8);
	if (len==4)
		i |= ((uint8_t)b[2] << 16) | ((uint8_t)b[3] << 24);

	return i;
}

/**
	@brief Converts char array to float
 */
float MockOscilloscope::BytesToFloat(char* b)
{
	union
	{
		float f;
		char b[4];
	} u;

	u.b[0] = b[0];
	u.b[1] = b[1];
	u.b[2] = b[2];
	u.b[3] = b[3];

	return u.f;

	//FIXME: There's probably a better way to do this, but it works
}

/**
	@brief Converts char array to double
 */
double MockOscilloscope::BytesToDouble(char* b)
{
	union
	{
		double d;
		char b[8];
	} u;

	u.b[0] = b[0];
	u.b[1] = b[1];
	u.b[2] = b[2];
	u.b[3] = b[3];
	u.b[4] = b[4];
	u.b[5] = b[5];
	u.b[6] = b[6];
	u.b[7] = b[7];

	return u.d;

	//FIXME: There's probably a better way to do this, but it works
}

/**
	@brief Imports waveforms from Comma Separated Value files
 */
bool MockOscilloscope::LoadCSV(const string& path)
{
	LogTrace("Importing CSV file \"%s\"\n", path.c_str());
	LogIndenter li;

	FILE* fp = fopen(path.c_str(), "r");
	if(!fp)
	{
		LogError("Failed to open file\n");
		return false;
	}

	vector<AnalogWaveform*> waveforms;

	char line[1024];
	size_t nrow = 0;
	size_t ncols = 0;
	vector<string> channel_names;
	while(!feof(fp))
	{
		nrow ++;

		if(!fgets(line, sizeof(line), fp))
			break;

		//Parse the samples for each row
		//TODO: be more efficient about this
		vector<float> row;
		string tmp;
		for(size_t i=0; i < sizeof(line); i++)
		{
			if(line[i] == '\0' || line[i] == ',')
			{
				float f;
				sscanf(tmp.c_str(), "%f", &f);
				row.push_back(f);

				if(line[i] == '\0')
					break;
				else
					tmp = "";
			}
			else
				tmp += line[i];
		}

		//If this is the first line, figure out how many columns we have.
		//First column is always timestamp in seconds.
		//TODO: support timestamp in abstract sample units instead
		if(nrow == 1)
		{
			ncols = row.size() - 1;

			//See if the first row is numeric
			bool numeric = true;
			for(size_t i=0; (i<sizeof(line)) && (line[i] != '\0'); i++)
			{
				if(!isdigit(line[i]) && !isspace(line[i]) && (line[i] != ',') && (line[i] != '.') )
				{
					numeric = false;
					break;
				}
			}

			if(!numeric)
			{
				LogTrace("Found %zu signal columns, with header row\n", ncols);

				//Extract names of the headers
				tmp = "";
				for(size_t i=0; i < sizeof(line); i++)
				{
					if(line[i] == '\0' || line[i] == ',')
					{
						channel_names.push_back(tmp);

						if(line[i] == '\0')
							break;
						else
							tmp = "";
					}
					else
						tmp += line[i];
				}

				//Discard name of timestamp column
				channel_names.erase(channel_names.begin());

				continue;
			}

			else
			{
				for(size_t i=0; i<ncols; i++)
					channel_names.push_back(string("CH") + to_string(i+1));

				LogTrace("Found %zu signal columns, no header row\n", ncols);
			}
		}

		//If we don't have any channels, create them
		if(GetChannelCount() == 0)
		{
			//Create the columns
			for(size_t i=0; i<ncols; i++)
			{
				//Create the channel
				auto chan = new OscilloscopeChannel(
					this,
					channel_names[i],
					OscilloscopeChannel::CHANNEL_TYPE_ANALOG,
					GetDefaultChannelColor(i),
					1,
					i,
					true);
				AddChannel(chan);
				chan->SetDefaultDisplayName();
			}
		}

		//Create waveforms if needed
		if(waveforms.empty())
		{
			for(size_t i=0; i<ncols; i++)
			{
				//Create the waveform for the channel
				auto wfm = new AnalogWaveform;
				wfm->m_timescale = 1;
				wfm->m_startTimestamp = 0;
				wfm->m_startFemtoseconds = 0;
				wfm->m_triggerPhase = 0;
				waveforms.push_back(wfm);
				GetChannel(i)->SetData(wfm, 0);
			}
		}

		int64_t timestamp = row[0] * FS_PER_SECOND;
		for(size_t i=0; i<ncols; i++)
		{
			if(i+1 >= row.size())
				break;

			auto w = waveforms[i];
			w->m_offsets.push_back(timestamp);
			w->m_samples.push_back(row[i+1]);

			//Extend last sample
			if(!w->m_durations.empty())
			{
				size_t last = w->m_durations.size() - 1;
				w->m_durations[last] = timestamp - w->m_offsets[last];
			}

			//Add duration for this sample
			w->m_durations.push_back(1);
		}
	}

	fclose(fp);

	//Calculate gain/offset for each channel
	for(size_t i=0; i<ncols; i++)
	{
		float vmin = FLT_MAX;
		float vmax = -FLT_MAX;

		for(auto v : waveforms[i]->m_samples)
		{
			vmax = max(vmax, (float)v);
			vmin = min(vmin, (float)v);
		}

		//LogDebug("vmax = %f, vmin = %f\n", vmax, vmin);

		auto chan = GetChannel(i);
		chan->SetVoltageRange(vmax - vmin);
		chan->SetOffset((vmin-vmax) / 2);
	}

	return true;
}

/**
	@brief Imports waveforms from Agilent/Keysight/Rigol binary capture files
 */
bool MockOscilloscope::LoadBIN(const string& path)
{
	LogTrace("Importing Agilent/Keysight/Rigol BIN file \"%s\"\n", path.c_str());
	LogIndenter li_f;

	//Open .bin file in binary mode
	FILE* fp = fopen(path.c_str(), "rb");
	if(!fp)
	{
		LogError("Failed to open file");
		return false;
	}

	//Parse file header
	FileHeader fh;
	fh.magic = ReadFromFile(2, fp);						//File signature ("AG")
	fh.version = ReadFromFile(2, fp);					//File format version
	fh.length = BytesToInt(ReadFromFile(4, fp), 4);		//Length of file in bytes
	fh.count = BytesToInt(ReadFromFile(4, fp), 4);		//Number of waveforms

	//Check file signature
	if(strcmp(fh.magic, "AG") != 0 && strcmp(fh.magic, "RG") != 0)
	{
		LogError("Unknown file format");
		return false;
	}

	//LogDebug("File size: %i KB\n", fh.length / 1024);
	//LogDebug("Waveforms: %i\n\n", (int)fh.count);

	//Load waveforms
	vector<AnalogWaveform*> waveforms;
	for(size_t i=0; i<fh.count; i++)
	{
		LogDebug("Waveform %i:\n", (int)i+1);
		LogIndenter li_w;

		//Parse waveform header
		WaveHeader wh;
		wh.size = BytesToInt(ReadFromFile(4, fp), 4);		//Waveform header length (0x8C)
		wh.type = BytesToInt(ReadFromFile(4, fp), 4);		//Waveform type
		wh.buffers = BytesToInt(ReadFromFile(4, fp), 4);	//Number of buffers
		wh.samples = BytesToInt(ReadFromFile(4, fp), 4);	//Number of samples
		wh.averaging = BytesToInt(ReadFromFile(4, fp), 4);	//Averaging count
		wh.duration = BytesToFloat(ReadFromFile(4, fp));	//Capture duration
		wh.start = BytesToDouble(ReadFromFile(8, fp));		//Display start time
		wh.interval = BytesToDouble(ReadFromFile(8, fp));	//Sample time interval
		wh.origin = BytesToDouble(ReadFromFile(8, fp));		//Capture origin time
		wh.x = BytesToInt(ReadFromFile(4, fp), 4);			//X axis unit
		wh.y = BytesToInt(ReadFromFile(4, fp), 4);			//Y axis unit
		wh.date = ReadFromFile(16, fp);						//Capture date
		wh.time = ReadFromFile(16, fp);						//Capture time
		char* hardware = ReadFromFile(24, fp);				//Hardware string (model + serial)
		wh.label = ReadFromFile(16, fp);					//Waveform label	
		wh.holdoff = BytesToDouble(ReadFromFile(8, fp));	//Trigger holdoff
		wh.segment = BytesToInt(ReadFromFile(4, fp), 4);	//Segment number
		wh.rate = (uint64_t)(1 / wh.interval);				//Calculated sample rate

		// Split hardware string
		int idx = 0;
		for(int c=0; c<24; c++)
		{
			if(hardware[c] == ':')
			{
				idx = c;
				break;
			}
		}

		//Frame model
		wh.frame = new char[idx + 1]();
		strncpy(wh.frame, hardware, idx);

		//Frame serial
		wh.serial = new char[24 - idx]();
		strncpy(wh.serial, hardware + idx + 1, 24 - idx - 1);

		//Log waveform info
		LogDebug("Samples:      %i\n", (int)wh.samples);
		LogDebug("Buffers:      %i\n", (int)wh.buffers);
		LogDebug("Type:         %i\n", wh.type);
		LogDebug("Duration:     %.*f us\n", 2, wh.duration * 1e6);
		LogDebug("Start:        %.*f us\n", 2, wh.start * 1e6);
		LogDebug("Interval:     %.*f ns\n", 2, wh.interval * 1e9);
		LogDebug("Origin:       %.*f us\n", 2, wh.origin * 1e6);
		LogDebug("Holdoff:      %.*f ms\n", 2, wh.holdoff * 1e3);
		LogDebug("Sample Rate:  %.*f Msps\n", 2, wh.rate / 1e6);
		LogDebug("Frame:        %s\n", wh.frame);
		LogDebug("Serial:       %s\n\n", wh.serial);

		//Set oscilloscope metadata
		m_vendor = "Agilent/Keysight/Rigol";
		m_name = wh.frame;
		m_serial = wh.serial;

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
		waveforms.push_back(wfm);
		chan->SetData(wfm, 0);

		//Loop through waveform buffers
		auto w = waveforms[i];
		float vmin = FLT_MAX;
		float vmax = -FLT_MAX;
		for(size_t j=0; j<wh.buffers; j++)
		{
			LogDebug("Buffer %i:\n", (int)j+1);
			LogIndenter li_b;

			//Parse waveform data header
			DataHeader dh;
			dh.size = BytesToInt(ReadFromFile(4, fp), 4);		//Waveform data header length
			dh.type = BytesToInt(ReadFromFile(2, fp), 2);		//Sample data type
			dh.depth = BytesToInt(ReadFromFile(2, fp), 2) * 8;	//Sample bit depth
			dh.length = BytesToInt(ReadFromFile(4, fp), 4);		//Data buffer length

    		LogDebug("Data Type:      %i\n", dh.type);
    		LogDebug("Sample depth:   %i bits\n", dh.depth);
    		LogDebug("Buffer length:  %i KB\n\n\n", dh.length / 1024);

			//Loop through waveform samples
			for(size_t k=0; k<wh.samples; k++)
			{
				//Handle different data types
				float sample;
				if(dh.type == 6 && dh.depth == 8)
				{
					//8-bit integer samples
					uint8_t sample_i = (int)ReadFromFile(1, fp)[0];
					sample = (float)sample_i;
				}
				else if(dh.type == 6 && dh.depth == 16)
				{
					//16-bit integer samples
					uint16_t sample_i = BytesToInt(ReadFromFile(2, fp), 2);
					sample = (float)sample_i;
				}
				else
				{
					//32-bit float samples
					sample = BytesToFloat(ReadFromFile(4, fp));
				}

				w->m_offsets.push_back(k);
				w->m_samples.push_back(sample);
				w->m_durations.push_back(1);

				vmax = max(vmax, sample);
				vmin = min(vmin, sample);
			}
		}

		//Calculate offset and range
		chan->SetVoltageRange((vmax-vmin) * 1.5);
		chan->SetOffset(-((vmax-abs(vmin)) / 2));
	}

	fclose(fp);

	return true;
}
