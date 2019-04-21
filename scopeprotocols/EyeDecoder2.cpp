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
#include "EyeRenderer.h"
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
	return new EyeRenderer(this);
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
	float tfix = 1.5 * waveform->m_timescale;
	float fwidth = m_width / 2.0f;
	float ymid = m_height / 2;
	for(auto& samp : waveform->m_samples)
	{
		//Stop when we get to the end
		if(iclock + 1 >= clock->GetDepth())
			break;

		//Look up time of the starting and ending clock edges
		int64_t tclock = clock->GetSampleStart(iclock) * clock->m_timescale;
		int64_t tend = clock->GetSampleStart(iclock+1) * clock->m_timescale;
		int64_t twidth = tend - tclock;
		awidth += twidth;
		nwidth ++;

		//Find time of this sample
		int64_t tstart = samp.m_offset * waveform->m_timescale + waveform->m_triggerPhase;

		//If it's past the end of the current UI, increment the clock
		int64_t offset = tstart - tclock;
		if(offset > twidth)
		{
			iclock ++;
			offset = tstart - tend;
		}

		//TODO: figure out where this is creeping in
		offset += tfix;

		//Find (and sanity check) the Y coordinate
		size_t pixel_y = (samp.m_sample * yscale) + ymid;
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

	double dt = GetTime() - start;
	total_frames ++;
	total_time += dt;
	LogTrace("Refresh took %.3f ms (avg %.3f)\n", dt * 1000, (total_time * 1000) / total_frames);
}
