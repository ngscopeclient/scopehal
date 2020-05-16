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
#include "Measurement.h"

using namespace std;

Measurement::CreateMapType Measurement::m_createprocs;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

Measurement::Measurement()
{
}

Measurement::~Measurement()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

size_t Measurement::GetInputCount()
{
	return m_signalNames.size();
}

string Measurement::GetInputName(size_t i)
{
	if(i < m_signalNames.size())
		return m_signalNames[i];
	else
	{
		LogError("Invalid channel index\n");
		return "";
	}
}

void Measurement::SetInput(size_t i, OscilloscopeChannel* channel)
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
		}
		m_channels[i] = channel;
	}
	else
	{
		LogError("Invalid channel index\n");
	}
}

void Measurement::SetInput(string name, OscilloscopeChannel* channel)
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

OscilloscopeChannel* Measurement::GetInput(size_t i)
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
// Enumeration

void Measurement::AddMeasurementClass(string name, CreateProcType proc)
{
	m_createprocs[name] = proc;
}

void Measurement::EnumMeasurements(vector<string>& names)
{
	for(CreateMapType::iterator it=m_createprocs.begin(); it != m_createprocs.end(); ++it)
		names.push_back(it->first);
}

Measurement* Measurement::CreateMeasurement(string protocol)
{
	if(m_createprocs.find(protocol) != m_createprocs.end())
		return m_createprocs[protocol]();

	LogError("Invalid measurement name\n");
	return NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Interpolation helpers

/**
	@brief Interpolates the actual time of a threshold crossing between two samples

	Simple linear interpolation for now (TODO sinc)

	@return Interpolated crossing time. 0=a, 1=a+1, fractional values are in between.
 */
float Measurement::InterpolateTime(AnalogWaveform* cap, size_t a, float voltage)
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
float Measurement::GetMinVoltage(AnalogWaveform* cap)
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
float Measurement::GetMaxVoltage(AnalogWaveform* cap)
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
float Measurement::GetAvgVoltage(AnalogWaveform* cap)
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
vector<size_t> Measurement::MakeHistogram(AnalogWaveform* cap, float low, float high, size_t bins)
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
float Measurement::GetBaseVoltage(AnalogWaveform* cap)
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
float Measurement::GetTopVoltage(AnalogWaveform* cap)
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FloatMeasurement

FloatMeasurement::FloatMeasurement(FloatMeasurementType type)
	: m_value(0)
	, m_type(type)
{
}

FloatMeasurement::~FloatMeasurement()
{

}

/**
	@brief Pretty-prints our value
 */
string FloatMeasurement::GetValueAsString()
{
	char tmp[128] = "";

	switch(m_type)
	{
		case TYPE_PERCENTAGE:
			snprintf(tmp, sizeof(tmp), "%.2f %%", m_value * 100);
			break;

		case TYPE_VOLTAGE:
			if(fabs(m_value) > 1)
				snprintf(tmp, sizeof(tmp), "%.3f V", m_value);
			else
				snprintf(tmp, sizeof(tmp), "%.2f mV", m_value * 1000);
			break;

		case TYPE_TIME:
			if(fabs(m_value) < 1e-9)
				snprintf(tmp, sizeof(tmp), "%.3f ps", m_value * 1e12);
			else if(fabs(m_value) < 1e-6)
				snprintf(tmp, sizeof(tmp), "%.3f ns", m_value * 1e9);
			else if(fabs(m_value) < 1e-3)
				snprintf(tmp, sizeof(tmp), "%.3f μs", m_value * 1e6);
			else
				snprintf(tmp, sizeof(tmp), "%.3f ms", m_value * 1e3);
			break;

		case TYPE_FREQUENCY:
			if(m_value > 1e6)
				snprintf(tmp, sizeof(tmp), "%.3f MHz", m_value * 1e-6);
			else if(m_value > 1e3)
				snprintf(tmp, sizeof(tmp), "%.3f kHz", m_value * 1e-3);
			else
				snprintf(tmp, sizeof(tmp), "%.2f Hz", m_value);
			break;

		case TYPE_BAUD:
			if(m_value > 1e9)
				snprintf(tmp, sizeof(tmp), "%.3f Gbps", m_value * 1e-9);
			else if(m_value > 1e6)
				snprintf(tmp, sizeof(tmp), "%.3f Mbps", m_value * 1e-6);
			else if(m_value > 1e3)
				snprintf(tmp, sizeof(tmp), "%.3f kbps", m_value * 1e-3);
			else
				snprintf(tmp, sizeof(tmp), "%.2f bps", m_value);
			break;
	}

	return tmp;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

string Measurement::SerializeConfiguration(IDTable& table, string nick)
{
	//Save basic info
	char tmp[1024];
	snprintf(tmp, sizeof(tmp), "                : \n");
	string config = tmp;
	snprintf(tmp, sizeof(tmp), "                    id:          %d\n", table.emplace(this));
	config += tmp;

	//Config
	snprintf(tmp, sizeof(tmp), "                    measurement: \"%s\"\n", GetMeasurementDisplayName().c_str());
	config += tmp;
	snprintf(tmp, sizeof(tmp), "                    nick:        \"%s\"\n", nick.c_str());
	config += tmp;
	snprintf(tmp, sizeof(tmp), "                    color:       \"%s\"\n", m_channels[0]->m_displaycolor.c_str());
	config += tmp;

	//Inputs
	snprintf(tmp, sizeof(tmp), "                    inputs: \n");
	config += tmp;
	for(size_t i=0; i<m_channels.size(); i++)
	{
		auto chan = m_channels[i];
		if(chan == NULL)
			snprintf(tmp, sizeof(tmp), "                        %s: 0\n", m_signalNames[i].c_str());
		else
			snprintf(tmp, sizeof(tmp), "                        %-20s %d\n", (m_signalNames[i] + ":").c_str(), table.emplace(chan));

		config += tmp;
	}

	/*
	//Parameters
	snprintf(tmp, sizeof(tmp), "                    parameters: %%\n");
	config += tmp;
	for(auto it : m_parameters)
	{
		snprintf(tmp, sizeof(tmp), "                        %-20s %s\n", (it.first+":").c_str(), it.second.ToString().c_str());
		config += tmp;
	}
	*/

	return config;
}
