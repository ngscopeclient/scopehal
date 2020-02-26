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

#include "../scopehal/scopehal.h"
#include "EyeDecoder2.h"
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

EyeCapture2::EyeCapture2(size_t width, size_t height)
	: m_width(width)
	, m_height(height)
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

EyeCapture2::~EyeCapture2()
{
	delete[] m_accumdata;
	m_accumdata = NULL;
	delete[] m_outdata;
	m_outdata = NULL;
}

size_t EyeCapture2::GetDepth() const
{
	return 0;
}

int64_t EyeCapture2::GetEndTime() const
{
	return 0;
}

int64_t EyeCapture2::GetSampleStart(size_t /*i*/) const
{
	return 0;
}

int64_t EyeCapture2::GetSampleLen(size_t /*i*/) const
{
	return 0;
}

bool EyeCapture2::EqualityTest(size_t /*i*/, size_t /*j*/) const
{
	return false;
}

bool EyeCapture2::SamplesAdjacent(size_t /*i*/, size_t /*j*/) const
{
	return false;
}

void EyeCapture2::Normalize()
{
	//Normalize it
	size_t len = m_width * m_height;

	int64_t nmax = 0;
	for(size_t i=0; i<len; i++)
		nmax = max(m_accumdata[i], nmax);
	if(nmax == 0)
		nmax = 1;
	float norm = 2.0f / nmax;

	for(size_t i=0; i<len; i++)
		m_outdata[i] = m_accumdata[i] * norm;

	//Once the output is normalized, check for any rows with no bin hits due to roundoff and interpolate into them.
	for(size_t y=1; y+1 < m_height; y++)
	{
		bool empty = true;
		for(size_t x=0; x<m_width; x++)
		{
			if(m_accumdata[y*m_width + x])
			{
				empty = false;
				break;
			}
		}

		if(empty)
		{
			for(size_t x=0; x<m_width; x++)
			{
				float out1 = m_outdata[(y-1)*m_width + x];
				float out2 = m_outdata[(y+1)*m_width + x];
				m_outdata[y*m_width + x] = (out1 + out2) / 2;
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

EyeDecoder2::EyeDecoder2(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_ANALYSIS)
{
	//Set up channels
	m_signalNames.push_back("din");
	m_channels.push_back(NULL);

	m_signalNames.push_back("clk");
	m_channels.push_back(NULL);

	m_uiWidth = 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

ChannelRenderer* EyeDecoder2::CreateRenderer()
{
	return NULL;
}

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
		m_channels[0]->m_displayname.c_str(),
		m_channels[1]->m_displayname.c_str());
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

	auto waveform = dynamic_cast<AnalogCapture*>(m_channels[0]->GetData());
	auto clock = dynamic_cast<DigitalCapture*>(m_channels[1]->GetData());
	if( (waveform == NULL) || (clock == NULL) )
	{
		SetData(NULL);
		return;
	}

	//Can't do much if we have no samples to work with
	if( (waveform->GetDepth() == 0) || (clock->GetDepth() == 0) )
	{
		SetData(NULL);
		return;
	}

	double start = GetTime();

	//Initialize the capture
	//TODO: timestamps? do we need those?
	EyeCapture2* cap = dynamic_cast<EyeCapture2*>(m_data);
	if(cap == NULL)
		cap = new EyeCapture2(m_width, m_height);
	cap->m_timescale = 1;
	int64_t* data = cap->GetAccumData();

	//Process the eye
	size_t iclock = 0;
	double awidth = 0;
	int64_t nwidth = 0;
	float yscale = m_height / m_channels[0]->GetVoltageRange();
	float fwidth = m_width / 2.0f;
	float ymid = m_height / 2;
	for(auto& samp : waveform->m_samples)
	{
		//Stop when we get to the end
		if(iclock + 1 >= clock->GetDepth())
			break;

		//Look up time of the starting and ending clock edges
		int64_t tclock = clock->GetSampleStart(iclock) * clock->m_timescale;
		//int64_t tend = clock->GetSampleStart(iclock+1) * clock->m_timescale;
		int64_t twidth = clock->GetSampleLen(iclock);
		awidth += twidth;
		nwidth ++;

		//Find time of this sample
		int64_t tstart = samp.m_offset * waveform->m_timescale + waveform->m_triggerPhase;

		//If it's past the end of the current UI, increment the clock
		int64_t offset = tstart - tclock;
		if(offset < 0)
			continue;
		if(offset > twidth)
		{
			iclock ++;
			offset -= twidth;
		}
		//LogDebug("offset = %ld, twidth = %ld\n",offset, twidth);

		//Find (and sanity check) the Y coordinate
		size_t pixel_y = round( (samp.m_sample * yscale) + ymid );
		if(pixel_y >= m_height)
			continue;
		int64_t* row = data + pixel_y*m_width;

		//Sampling clock is the middle of the UI, not the start.
		//Anything more than half a UI right of the clock is negative.
		int64_t halfwidth = twidth/2;
		if(offset > halfwidth)
			offset = -twidth + offset;
		if(offset < -halfwidth)
			continue;

		//Plot each point 3 times for center/left/right portions of the eye
		//Map -twidth to +twidth to 0...m_width
		int64_t xpos[] = {offset, offset + twidth, -twidth + offset };
		float scale = fwidth / twidth;
		for(auto x : xpos)
		{
			size_t pixel_x = round((x + twidth) * scale);
			if(pixel_x < m_width)
				row[pixel_x] ++;
		}
	}
	m_uiWidth = round(awidth / nwidth);

	cap->Normalize();
	SetData(cap);

	double dt = GetTime() - start;
	total_frames ++;
	total_time += dt;
	LogTrace("Refresh took %.3f ms (avg %.3f)\n", dt * 1000, (total_time * 1000) / total_frames);
}
