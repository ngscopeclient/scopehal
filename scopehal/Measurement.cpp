/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2019 Andrew D. Zonenberg                                                                          *
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
float Measurement::InterpolateTime(AnalogCapture* cap, size_t a, float voltage)
{
	//If the voltage isn't between the two points, abort
	float fa = (*cap)[a];
	float fb = (*cap)[a+1];
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
float Measurement::GetMinVoltage(AnalogCapture* cap)
{
	//Loop over samples and find the minimum
	float tmp = FLT_MAX;
	for(auto sample : *cap)
	{
		if((float)sample < tmp)
			tmp = sample;
	}
	return tmp;
}

/**
	@brief Gets the highest voltage of a waveform
 */
float Measurement::GetMaxVoltage(AnalogCapture* cap)
{
	//Loop over samples and find the maximum
	float tmp = FLT_MIN;
	for(auto sample : *cap)
	{
		if((float)sample > tmp)
			tmp = sample;
	}
	return tmp;
}

/**
	@brief Gets the average voltage of a waveform
 */
float Measurement::GetAvgVoltage(AnalogCapture* cap)
{
	//Loop over samples and find the average
	//TODO: more numerically stable summation algorithm for deep captures
	double sum = 0;
	for(auto sample : *cap)
		sum += (float)sample;
	return sum / cap->GetDepth();
}

/**
	@brief Gets the average period of a waveform (measured from rising edge to rising edge with +/- 10% hysteresis)
 */
float Measurement::GetPeriod(AnalogCapture* cap)
{
	//Find min, max, and average voltage of the signal
	float low = GetMinVoltage(cap);
	float high = GetMaxVoltage(cap);
	float avg = GetAvgVoltage(cap);

	//Hysteresis: aim 10% above and below the average
	float delta = (high - low) / 10;
	float vlo = avg - delta;
	float vhi = avg + delta;

	bool first = true;
	size_t prev_rising = 0;
	bool current_state = false;
	double delta_sum = 0;
	double delta_count = 0;
	for(size_t i=1; i<cap->GetDepth(); i++)
	{
		//Go from high to low
		float v = (*cap)[i];
		if(current_state && (v < vlo) )
			current_state = false;

		//Go from low to high
		else if(!current_state && (v > vhi) )
		{
			//If we've seen at least one rising edge, calculate the delta
			if(!first)
			{
				//Find the approximate time of the zero crossing, then interpolate
				float delta_samples = cap->GetSampleStart(i) - cap->GetSampleStart(prev_rising);
				delta_samples += InterpolateTime(cap, i-1, vhi);
				delta_samples -= InterpolateTime(cap, prev_rising-1, vhi);

				delta_sum += delta_samples * cap->m_timescale;
				delta_count ++;
			}

			first = false;
			prev_rising = i;
			current_state = true;
		}
	}

	double avg_ps = delta_sum / delta_count;
	return avg_ps * 1e-12f;
}

/**
	@brief Makes a histogram from a waveform with the specified number of bins.

	Any values outside the range are clamped (put in bin 0 or bins-1 as appropriate).

	@param low	Low endpoint of the histogram (volts)
	@param high High endpoint of the histogram (volts)
	@param bins	Number of histogram bins
 */
vector<size_t> Measurement::MakeHistogram(AnalogCapture* cap, float low, float high, size_t bins)
{
	vector<size_t> ret;
	for(size_t i=0; i<bins; i++)
		ret.push_back(0);

	float delta = high-low;

	for(float v : *cap)
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
float Measurement::GetBaseVoltage(AnalogCapture* cap)
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
float Measurement::GetTopVoltage(AnalogCapture* cap)
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

/**
	@brief Gets the average rise time of a waveform

	The low and high thresholds are fractional values, e.g. 0.2 and 0.8 for 20-80% rise time.
 */
float Measurement::GetRiseTime(AnalogCapture* cap, float low, float high)
{
	float base = GetBaseVoltage(cap);
	float top = GetTopVoltage(cap);
	float delta = top-base;

	float start = low*delta + base;
	float end = high*delta + base;

	//Find all of the rising edges and count stuff
	enum
	{
		STATE_UNKNOWN,
		STATE_RISING,
		STATE_FALLING,
		STATE_LOW
	} state = STATE_UNKNOWN;
	size_t edge_start = 0;
	double delta_sum = 0;
	double delta_count = 0;
	for(size_t i=1; i<cap->GetDepth(); i++)
	{
		float v = (*cap)[i];

		switch(state)
		{
			//Starting out
			case STATE_UNKNOWN:
				if(v > end)
					state = STATE_FALLING;
				break;

			//Waiting for falling edge
			case STATE_FALLING:
				if(v < start)
					state = STATE_LOW;
				break;

			//Wait for start of rising edge
			case STATE_LOW:
				if(v > start)
				{
					edge_start = i;
					state = STATE_RISING;
				}
				break;

			//Wait for end of rising edge
			case STATE_RISING:
				if(v > end)
				{
					//Interpolate end point
					float delta_samples = cap->GetSampleStart(i) - cap->GetSampleStart(edge_start);
					delta_samples += InterpolateTime(cap, i-1, end);
					delta_samples -= InterpolateTime(cap, edge_start-1, start);

					delta_sum += delta_samples * cap->m_timescale;
					delta_count ++;

					state = STATE_FALLING;
				}
				break;
		}
	}

	double avg_ps = delta_sum / delta_count;
	return avg_ps * 1e-12f;
}

/**
	@brief Gets the average fall time of a waveform

	The low and high thresholds are fractional values, e.g. 0.2 and 0.8 for 20-80% rise time.
 */
float Measurement::GetFallTime(AnalogCapture* cap, float low, float high)
{
	float base = GetBaseVoltage(cap);
	float top = GetTopVoltage(cap);
	float delta = top-base;

	float start = high*delta + base;
	float end = low*delta + base;

	//Find all of the falling edges and count stuff
	enum
	{
		STATE_UNKNOWN,
		STATE_RISING,
		STATE_FALLING,
		STATE_HIGH
	} state = STATE_UNKNOWN;
	size_t edge_start = 0;
	double delta_sum = 0;
	double delta_count = 0;
	for(size_t i=1; i<cap->GetDepth(); i++)
	{
		float v = (*cap)[i];

		switch(state)
		{
			//Starting out
			case STATE_UNKNOWN:
				if(v > start)
					state = STATE_HIGH;
				break;

			//Waiting for falling edge
			case STATE_RISING:
				if(v > start)
					state = STATE_HIGH;
				break;

			//Wait for start of falling edge
			case STATE_HIGH:
				if(v < start)
				{
					edge_start = i;
					state = STATE_FALLING;
				}
				break;

			//Wait for end of falling edge
			case STATE_FALLING:
				if(v < end)
				{
					//Interpolate end point
					float delta_samples = cap->GetSampleStart(i) - cap->GetSampleStart(edge_start);
					delta_samples += InterpolateTime(cap, i-1, end);
					delta_samples -= InterpolateTime(cap, edge_start-1, start);

					delta_sum += delta_samples * cap->m_timescale;
					delta_count ++;

					state = STATE_RISING;
				}
				break;
		}
	}

	double avg_ps = delta_sum / delta_count;
	return avg_ps * 1e-12f;
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
