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

#include "../scopehal/scopehal.h"
#include "EyeDecoder2.h"
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

EyeWaveform::EyeWaveform(size_t width, size_t height, float center)
	: m_uiWidth(1)
	, m_saturationLevel(1)
	, m_width(width)
	, m_height(height)
	, m_totalUIs(0)
	, m_centerVoltage(center)
{
	size_t npix = width*height;
	m_accumdata = new int64_t[npix];
	m_outdata = new float[npix];
	for(size_t i=0; i<npix; i++)
	{
		m_outdata[i] = 0;
		m_accumdata[i] = 0;
	}
}

EyeWaveform::~EyeWaveform()
{
	delete[] m_accumdata;
	m_accumdata = NULL;
	delete[] m_outdata;
	m_outdata = NULL;
}

void EyeWaveform::Normalize()
{
	//Normalize it
	size_t len = m_width * m_height;

	//Find the peak amplitude
	int64_t nmax = 0;
	for(size_t i=0; i<len; i++)
		nmax = max(m_accumdata[i], nmax);
	if(nmax == 0)
		nmax = 1;
	float norm = 2.0f / nmax;

	/*
		Normalize with saturation
		Saturation level of 1.0 means mapping all values to [0, 1].
		2.0 means mapping values to [0, 2] and saturating anything above 1.
	 */
	norm *= m_saturationLevel;
	for(size_t i=0; i<len; i++)
		m_outdata[i] = min(1.0f, m_accumdata[i] * norm);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

EyeDecoder2::EyeDecoder2(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_EYE, color, CAT_ANALYSIS)
{
	//Set up channels
	m_signalNames.push_back("din");
	m_channels.push_back(NULL);

	m_signalNames.push_back("clk");
	m_channels.push_back(NULL);

	m_uiWidth = 0;
	m_width = 0;
	m_height = 0;

	m_saturationName = "Saturation Level";
	m_parameters[m_saturationName] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_FLOAT);
	m_parameters[m_saturationName].SetFloatVal(1);

	m_centerName = "Center Voltage";
	m_parameters[m_centerName] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_FLOAT);
	m_parameters[m_centerName].SetFloatVal(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool EyeDecoder2::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	if( (i == 1) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string EyeDecoder2::GetProtocolName()
{
	return "Eye pattern";
}

void EyeDecoder2::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "Eye(%s, %s)",
		(m_channels[0] == NULL) ? "NULL" : m_channels[0]->m_displayname.c_str(),
		(m_channels[1] == NULL) ? "NULL" : m_channels[1]->m_displayname.c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

bool EyeDecoder2::IsOverlay()
{
	return false;
}

bool EyeDecoder2::NeedsConfig()
{
	return true;
}

double EyeDecoder2::GetVoltageRange()
{
	return m_channels[0]->GetVoltageRange();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

/*

bool EyeDecoder::DetectModulationLevels(AnalogCapture* din, EyeCapture* cap)
{
	LogDebug("Detecting modulation levels\n");
	LogIndenter li;

	//Find the min/max voltage of the signal (used to set default bounds for the render).
	//Additionally, generate a histogram of voltages. We need this to configure the trigger(s) correctly
	//and do measurements on the eye opening(s) - since MLT-3, PAM-x, etc have multiple openings.
	cap->m_minVoltage = 999;
	cap->m_maxVoltage = -999;
	map<int, int64_t> vhist;							//1 mV bins
	for(size_t i=0; i<din->m_samples.size(); i++)
	{
		AnalogSample sin = din->m_samples[i];
		float f = sin;

		vhist[f * 1000] ++;

		if(f > cap->m_maxVoltage)
			cap->m_maxVoltage = f;
		if(f < cap->m_minVoltage)
			cap->m_minVoltage = f;
	}
	LogDebug("Voltage range is %.3f to %.3f V\n", cap->m_minVoltage, cap->m_maxVoltage);

	//Crunch the histogram to find the number of signal levels in use.
	//We're looking for peaks of significant height (25% of maximum or more) and not too close to another peak.
	float dv = cap->m_maxVoltage - cap->m_minVoltage;
	int neighborhood = floor(dv * 50);	//dV/20 converted to mV
	LogDebug("Looking for levels at least %d mV apart\n", neighborhood);
	int64_t maxpeak = 0;
	for(auto it : vhist)
	{
		if(it.second > maxpeak)
			maxpeak = it.second;
	}
	LogDebug("Highest histogram peak is %ld points\n", maxpeak);

	int64_t peakthresh = maxpeak/8;
	int64_t second_peak = 0;
	double second_weighted = 0;
	for(auto it : vhist)
	{
		int64_t count = it.second;
		//If we're pretty close to a taller peak (within neighborhood mV) then don't do anything
		int mv = it.first;
		bool bigger = false;
		for(int v=mv-neighborhood; v<=mv+neighborhood; v++)
		{
			auto jt = vhist.find(v);
			if(jt == vhist.end())
				continue;
			if(jt->second > count)
			{
				bigger = true;
				continue;
			}
		}

		if(bigger)
			continue;

		//Search the neighborhood around us and do a weighted average to find the center of the bin
		int64_t weighted = 0;
		int64_t wcount = 0;
		for(int v=mv-neighborhood; v<=mv+neighborhood; v++)
		{
			auto jt = vhist.find(v);
			if(jt == vhist.end())
				continue;

			int64_t c = jt->second;
			wcount += c;
			weighted += c*v;
		}

		if(count < peakthresh)
		{
			//Skip peaks that aren't tall enough... but still save the second highest
			if(count > second_peak)
			{
				second_peak = count;
				second_weighted = weighted * 1e-3f / wcount;
			}
			continue;
		}

		cap->m_signalLevels.push_back(weighted * 1e-3f / wcount);
	}

	//Special case: if the signal has only one level it might be NRZ with a really low duty cycle
	//Add the second highest peak in this case
	if(cap->m_signalLevels.size() == 1)
		cap->m_signalLevels.push_back(second_weighted);

	sort(cap->m_signalLevels.begin(), cap->m_signalLevels.end());
	LogDebug("    Signal appears to be using %d-level modulation\n", (int)cap->m_signalLevels.size());
	for(auto v : cap->m_signalLevels)
		LogDebug("        %6.3f V\n", v);

	//Now that signal levels are sorted, make sure they're spaced well.
	//If we have levels that are too close to each other, skip them
	for(size_t i=0; i<cap->m_signalLevels.size()-1; i++)
	{
		float delta = fabs(cap->m_signalLevels[i] - cap->m_signalLevels[i+1]);
		LogDebug("Delta at i=%zu is %.3f\n", i, delta);

		//TODO: fine tune this threshold adaptively based on overall signal amplitude?
		if(delta < 0.175)
		{
			LogIndenter li;
			LogDebug("Too small\n");

			//Remove the innermost point (closer to zero)
			//This is us if we're positive, but the next one if negative!
			if(cap->m_signalLevels[i] < 0)
				cap->m_signalLevels.erase(cap->m_signalLevels.begin() + (i+1) );
			else
				cap->m_signalLevels.erase(cap->m_signalLevels.begin() + i);
		}
	}

	//Figure out decision points (eye centers)
	//FIXME: This doesn't work well for PAM! Only MLT*
	for(size_t i=0; i<cap->m_signalLevels.size()-1; i++)
	{
		float vlo = cap->m_signalLevels[i];
		float vhi = cap->m_signalLevels[i+1];
		cap->m_decisionPoints.push_back(vlo + (vhi-vlo)/2);
	}
	//LogDebug("    Decision points:\n");
	//for(auto v : cap->m_decisionPoints)
	//	LogDebug("        %6.3f V\n", v);

	//Sanity check
	if(cap->m_signalLevels.size() < 2)
	{
		LogDebug("Couldn't find at least two distinct symbol voltages\n");
		delete cap;
		return false;
	}

	return true;
}
*/

void EyeDecoder2::ClearSweeps()
{
	if(m_data != NULL)
	{
		delete m_data;
		m_data = NULL;
	}
}

double EyeDecoder2::GetOffset()
{
	return -m_parameters[m_centerName].GetFloatVal();
}

void EyeDecoder2::Refresh()
{
	static double total_time = 0;
	static double total_frames = 0;

	LogIndenter li;

	//Get the input data
	if( (m_channels[0] == NULL) || (m_channels[1] == NULL) )
	{
		SetData(NULL);
		return;
	}

	auto waveform = dynamic_cast<AnalogWaveform*>(m_channels[0]->GetData());
	auto clock = dynamic_cast<DigitalWaveform*>(m_channels[1]->GetData());
	if( (waveform == NULL) || (clock == NULL) )
	{
		SetData(NULL);
		return;
	}

	//Can't do much if we have no samples to work with
	size_t cend = clock->m_samples.size();
	if( (waveform->m_samples.size() == 0) || !cend )
	{
		SetData(NULL);
		return;
	}

	double start = GetTime();

	//If center of the eye was changed, reset existing eye data
	EyeWaveform* cap = dynamic_cast<EyeWaveform*>(m_data);
	double center = m_parameters[m_centerName].GetFloatVal();
	if(cap)
	{
		if(abs(cap->GetCenterVoltage() - center) > 0.001)
		{
			delete cap;
			cap = NULL;
		}
	}

	//Initialize the capture
	//TODO: timestamps? do we need those?
	if(cap == NULL)
		cap = new EyeWaveform(m_width, m_height, center);
	cap->m_saturationLevel = m_parameters[m_saturationName].GetFloatVal();
	cap->m_timescale = 1;
	int64_t* data = cap->GetAccumData();

	//Calculate average period of the clock
	//TODO: All of this code assumes a fully RLE'd clock with one sample per toggle.
	//We probably need a preprocessing filter to handle analog etc clock sources.
	double tlastclk = clock->m_offsets[cend-1] + clock->m_durations[cend-1];
	m_uiWidth = tlastclk / cend;
	cap->m_uiWidth = m_uiWidth;

	//Process the eye
	size_t iclock = 0;
	float yscale = m_height / m_channels[0]->GetVoltageRange();
	int64_t hwidth = m_width / 2;
	float fwidth = m_width / 2.0f;
	float ymid = m_height / 2;
	float yoff = -center*yscale + ymid;
	size_t wend = waveform->m_samples.size()-1;
	int64_t halfwidth = m_uiWidth / 2;
	float scale = fwidth / m_uiWidth;
	int64_t tscale = round(m_uiWidth * scale);
	for(size_t i=0; i<wend; i++)
	{
		//Stop when we get to the end of the clock
		if(iclock + 1 >= cend)
			break;

		//Find time of this sample.
		//If it's past the end of the current UI, move to the next clock edge
		int64_t twidth = clock->m_durations[iclock];
		int64_t tstart = waveform->m_offsets[i] * waveform->m_timescale + waveform->m_triggerPhase;
		int64_t offset = tstart - clock->m_offsets[iclock] * clock->m_timescale;
		if(offset < 0)
			continue;
		if(offset > twidth)
		{
			iclock ++;
			offset -= twidth;
		}

		//Sampling clock is the middle of the UI, not the start.
		//Anything more than half a UI right of the clock is negative.
		if(offset > halfwidth)
			offset = offset - twidth;
		if(offset < -halfwidth)
			continue;

		//Interpolate voltage
		int64_t dt = (waveform->m_offsets[i+1] - waveform->m_offsets[i]) * waveform->m_timescale;
		float pixel_x_f = offset * scale;
		float pixel_x_fround = floor(pixel_x_f);
		int64_t pixel_x_round = pixel_x_fround + hwidth;
		float dv = waveform->m_samples[i+1] - waveform->m_samples[i];
		float dx_frac = (pixel_x_f - pixel_x_fround ) / (dt * scale );
		float nominal_voltage = waveform->m_samples[i] + dv*dx_frac;

		//Find (and sanity check) the Y coordinate
		float nominal_pixel_y = nominal_voltage*yscale + yoff;
		size_t y1 = static_cast<size_t>(nominal_pixel_y);
		if(y1 >= (m_height-1))
			continue;

		//Calculate how much of the pixel's intensity to put in each row
		float yfrac = nominal_pixel_y - y1;
		int bin2 = yfrac * 64;
		int bin1 = 64 - bin2;
		int64_t* row1 = data + y1*m_width;
		int64_t* row2 = row1 + m_width;

		//Plot each point 3 times for center/left/right portions of the eye
		//Map -twidth to +twidth to 0...m_width
		int64_t xpos[] = {pixel_x_round, pixel_x_round + tscale, -tscale + pixel_x_round};
		for(auto x : xpos)
		{
			if( (x < (int64_t)m_width) && (x >= 0) )
			{
				row1[x] += bin1;
				row2[x] += bin2;
			}
		}
	}

	//Count total number of UIs we've integrated
	cap->IntegrateUIs(clock->m_samples.size());

	cap->Normalize();
	SetData(cap);

	double dt = GetTime() - start;
	total_frames ++;
	total_time += dt;
	LogTrace("Refresh took %.3f ms (avg %.3f)\n", dt * 1000, (total_time * 1000) / total_frames);
}
