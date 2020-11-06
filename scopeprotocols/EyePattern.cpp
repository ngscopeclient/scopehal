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
#include "EyePattern.h"
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

EyeWaveform::EyeWaveform(size_t width, size_t height, float center)
	: m_uiWidth(0)
	, m_saturationLevel(1)
	, m_width(width)
	, m_height(height)
	, m_totalUIs(0)
	, m_centerVoltage(center)
	, m_maskHitRate(0)
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
	//Preprocessing
	int64_t nmax = 0;
	int64_t halfwidth = m_width/2;
	size_t blocksize = halfwidth * sizeof(int64_t);
	for(size_t y=0; y<m_height; y++)
	{
		int64_t* row = m_accumdata + y*m_width;

		//Find peak amplitude
		for(size_t x=halfwidth; x<m_width; x++)
			nmax = max(row[x], nmax);

		//Copy right half to left half
		memcpy(row, row+halfwidth, blocksize);

		//Fix singularity at midpoint (TODO: better option)
		row[halfwidth+1] = row[halfwidth+2];
		row[halfwidth] = row[halfwidth-1];
	}
	if(nmax == 0)
		nmax = 1;
	float norm = 2.0f / nmax;

	/*
		Normalize with saturation
		Saturation level of 1.0 means mapping all values to [0, 1].
		2.0 means mapping values to [0, 2] and saturating anything above 1.
	 */
	norm *= m_saturationLevel;
	size_t len = m_width * m_height;
	for(size_t i=0; i<len; i++)
		m_outdata[i] = min(1.0f, m_accumdata[i] * norm);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

EyePattern::EyePattern(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_EYE, color, CAT_ANALYSIS)
	, m_height(1)
	, m_width(1)
	, m_xoff(0)
	, m_xscale(0)
	, m_lastClockAlign(ALIGN_CENTER)
	, m_saturationName("Saturation Level")
	, m_centerName("Center Voltage")
	, m_maskName("Mask")
	, m_polarityName("Clock Edge")
	, m_vmodeName("Vertical Scale Mode")
	, m_rangeName("Vertical Range")
	, m_clockAlignName("Clock Alignment")
{
	//Set up channels
	CreateInput("din");
	CreateInput("clk");

	m_parameters[m_saturationName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_saturationName].SetFloatVal(1);

	m_parameters[m_centerName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_centerName].SetFloatVal(0);

	m_parameters[m_maskName] = FilterParameter(FilterParameter::TYPE_FILENAME, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_maskName].SetFileName("");
	m_parameters[m_maskName].m_fileFilterMask = "*.yml";
	m_parameters[m_maskName].m_fileFilterName = "YAML files (*.yml)";

	m_parameters[m_polarityName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_polarityName].AddEnumValue("Rising", CLOCK_RISING);
	m_parameters[m_polarityName].AddEnumValue("Falling", CLOCK_FALLING);
	m_parameters[m_polarityName].AddEnumValue("Both", CLOCK_BOTH);
	m_parameters[m_polarityName].SetIntVal(CLOCK_BOTH);

	m_parameters[m_vmodeName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_vmodeName].AddEnumValue("Auto", RANGE_AUTO);
	m_parameters[m_vmodeName].AddEnumValue("Fixed", RANGE_FIXED);

	m_parameters[m_rangeName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_rangeName].SetFloatVal(0.25);

	m_parameters[m_clockAlignName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_clockAlignName].AddEnumValue("Center", ALIGN_CENTER);
	m_parameters[m_clockAlignName].AddEnumValue("Edge", ALIGN_EDGE);
	m_parameters[m_clockAlignName].SetIntVal(ALIGN_CENTER);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool EyePattern::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	if( (i == 1) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string EyePattern::GetProtocolName()
{
	return "Eye pattern";
}

void EyePattern::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "Eye(%s, %s)",
		GetInputDisplayName(0).c_str(),
		GetInputDisplayName(1).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

bool EyePattern::IsOverlay()
{
	return false;
}

bool EyePattern::NeedsConfig()
{
	return true;
}

double EyePattern::GetVoltageRange()
{
	if(m_parameters[m_vmodeName].GetIntVal() == RANGE_AUTO)
		return m_inputs[0].m_channel->GetVoltageRange();
	else
		return m_parameters[m_rangeName].GetFloatVal();
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

void EyePattern::ClearSweeps()
{
	SetData(NULL, 0);
}

double EyePattern::GetOffset()
{
	return -m_parameters[m_centerName].GetFloatVal();
}

void EyePattern::Refresh()
{
	static double total_time = 0;
	static double total_frames = 0;

	LogIndenter li;

	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto waveform = GetAnalogInputWaveform(0);
	auto clock = GetDigitalInputWaveform(1);
	double start = GetTime();

	//If center of the eye was changed, reset existing eye data
	EyeWaveform* cap = dynamic_cast<EyeWaveform*>(GetData(0));
	double center = m_parameters[m_centerName].GetFloatVal();
	if(cap)
	{
		if(abs(cap->GetCenterVoltage() - center) > 0.001)
		{
			SetData(NULL, 0);
			cap = NULL;
		}
	}

	//If clock alignment was changed, reset existing eye data
	ClockAlignment clock_align = static_cast<ClockAlignment>(m_parameters[m_clockAlignName].GetIntVal());
	if(m_lastClockAlign != clock_align)
	{
		SetData(NULL, 0);
		cap = NULL;
		m_lastClockAlign = clock_align;
	}

	//Load the mask, if needed
	string maskpath = m_parameters[m_maskName].GetFileName();
	if(maskpath != m_mask.GetFileName())
		m_mask.Load(maskpath);

	//Initialize the capture
	//TODO: timestamps? do we need those?
	if(cap == NULL)
		cap = ReallocateWaveform();
	cap->m_saturationLevel = m_parameters[m_saturationName].GetFloatVal();
	int64_t* data = cap->GetAccumData();

	//Find all toggles in the clock
	vector<int64_t> clock_edges;
	switch(m_parameters[m_polarityName].GetIntVal())
	{
		case CLOCK_RISING:
			FindRisingEdges(clock, clock_edges);
			break;

		case CLOCK_FALLING:
			FindFallingEdges(clock, clock_edges);
			break;

		case CLOCK_BOTH:
			FindZeroCrossings(clock, clock_edges);
			break;
	}

	//Calculate the nominal UI width
	if(cap->m_uiWidth < FLT_EPSILON)
		RecalculateUIWidth();

	//Shift the clock by half a UI if it's edge aligned
	//All of the eye creation logic assumes a center aligned clock.
	if(clock_align == ALIGN_EDGE)
	{
		for(size_t i=0; i<clock_edges.size(); i++)
			clock_edges[i] += cap->m_uiWidth / 2;
	}

	uint32_t prng = 0xdeadbeef;

	//Precompute some scaling factors
	float yscale = m_height / GetVoltageRange();
	float ymid = m_height / 2;
	float yoff = -center*yscale + ymid;
	float xtimescale = waveform->m_timescale * m_xscale;
	float xscale_div255 = m_xscale / 255.0f;
	float xscale_div2 = m_xscale / 2;

	//Process the eye
	size_t cend = clock_edges.size() - 1;
	size_t iclock = 0;
	size_t wend = waveform->m_samples.size()-1;
	size_t ymax = m_height - 1;
	int64_t xmax = m_width;
	if(m_xscale > FLT_EPSILON)
	{
		for(size_t i=0; i<wend && iclock < cend; i++)
		{
			//Find time of this sample.
			//If it's past the end of the current UI, move to the next clock edge
			int64_t tstart = waveform->m_offsets[i] * waveform->m_timescale + waveform->m_triggerPhase;
			int64_t offset = tstart - clock_edges[iclock];
			if(offset < -10)
				continue;
			size_t nextclk = iclock + 1;
			int64_t tnext = clock_edges[nextclk];
			int64_t twidth = tnext - clock_edges[iclock];
			if(offset > twidth)
			{
				//Move to the next clock edge
				iclock ++;
				if(iclock >= cend)
					break;

				//Figure out the offset to the next edge
				offset = tstart - tnext;
			}

			//Antialiasing: jitter the fractional X position by up to 1ps to fill in blank spots
			int64_t dt = waveform->m_offsets[i+1] - waveform->m_offsets[i];
			float pixel_x_f = (offset - m_xoff) * m_xscale;
			float pixel_x_fround = floor(pixel_x_f);
			float dx_frac = (pixel_x_f - pixel_x_fround ) / (dt * xtimescale );
			pixel_x_f += (prng & 0xff) * xscale_div255 - xscale_div2;
			prng = 0x343fd * prng + 0x269ec3;

			//Early out if off the end of the plot
			int64_t pixel_x_round = round(pixel_x_f);
			if(pixel_x_round >= xmax)
				continue;

			//Interpolate voltage, early out if clipping
			float dv = waveform->m_samples[i+1] - waveform->m_samples[i];
			float nominal_voltage = waveform->m_samples[i] + dv*dx_frac;
			float nominal_pixel_y = nominal_voltage*yscale + yoff;
			size_t y1 = static_cast<size_t>(nominal_pixel_y);
			if(y1 >= ymax)
				continue;

			//Calculate how much of the pixel's intensity to put in each row
			float yfrac = nominal_pixel_y - floor(nominal_pixel_y);
			float bin2 = yfrac * 64;
			float bin1 = 64 - bin2;
			int64_t* pix = data + y1*m_width + pixel_x_round;

			//Plot each point (this only draws the right half of the eye, we copy to the left later)
			pix[0] += bin1;
			pix[m_width] += bin2;
		}
	}

	//Count total number of UIs we've integrated
	cap->IntegrateUIs(clock_edges.size());
	cap->Normalize();

	//If we have an eye mask, prepare it for processing
	if(m_mask.GetFileName() != "")
		DoMaskTest(cap);

	double dt = GetTime() - start;
	total_frames ++;
	total_time += dt;
	LogTrace("Refresh took %.3f ms (avg %.3f)\n", dt * 1000, (total_time * 1000) / total_frames);
}

EyeWaveform* EyePattern::ReallocateWaveform()
{
	auto cap = new EyeWaveform(m_width, m_height, m_parameters[m_centerName].GetFloatVal());
	cap->m_timescale = 1;
	SetData(cap, 0);
	return cap;
}

void EyePattern::RecalculateUIWidth()
{
	auto cap = dynamic_cast<EyeWaveform*>(GetData(0));
	if(!cap)
		cap = ReallocateWaveform();

	auto clock = GetDigitalInputWaveform(1);
	if(!clock)
		return;

	//Find all toggles in the clock
	vector<int64_t> clock_edges;
	switch(m_parameters[m_polarityName].GetIntVal())
	{
		case CLOCK_RISING:
			FindRisingEdges(clock, clock_edges);
			break;

		case CLOCK_FALLING:
			FindFallingEdges(clock, clock_edges);
			break;

		case CLOCK_BOTH:
			FindZeroCrossings(clock, clock_edges);
			break;
	}

	//Find width of each UI
	vector<int64_t> ui_widths;
	for(size_t i=0; i<clock_edges.size()-1; i++)
		ui_widths.push_back(clock_edges[i+1] - clock_edges[i]);

	//Need to average at least ten UIs to get meaningful data
	size_t nuis = ui_widths.size();
	if(nuis > 10)
	{
		//Sort, discard the top and bottom 10%, and average the rest to calculate nominal width
		sort(ui_widths.begin(), ui_widths.end());
		size_t navg = 0;
		int64_t total = 0;
		for(size_t i = nuis/10; i <= nuis*9/10; i++)
		{
			total += ui_widths[i];
			navg ++;
		}

		cap->m_uiWidth = (1.0 * total) / navg;
	}
}

/**
	@brief Checks the current capture against the eye mask
 */
void EyePattern::DoMaskTest(EyeWaveform* cap)
{
	//TODO: performance optimization, don't re-render mask every waveform, only when we resize

	//Create the Cairo surface we're drawing on
	Cairo::RefPtr< Cairo::ImageSurface > surface =
		Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, m_width, m_height);
	Cairo::RefPtr< Cairo::Context > cr = Cairo::Context::create(surface);

	//Set up transformation to match GL's bottom-left origin
	//cr->translate(0, m_height);
	//cr->scale(1, -1);

	//Clear to a blank background
	cr->set_source_rgba(0, 0, 0, 1);
	cr->rectangle(0, 0, m_width, m_height);
	cr->fill();

	//Software rendering
	float yscale = m_height / GetVoltageRange();
	m_mask.RenderForAnalysis(
		cr,
		cap,
		m_xscale,
		m_xoff,
		yscale,
		0,
		m_height);

	auto accum = cap->GetAccumData();

	//Test each pixel of the eye pattern against the mask
	uint32_t* data = reinterpret_cast<uint32_t*>(surface->get_data());
	int stride = surface->get_stride() / sizeof(uint32_t);
	size_t total = 0;
	size_t hits = 0;
	for(size_t y=0; y<m_height; y++)
	{
		auto row = data + (y*stride);
		auto eyerow = accum + (y*m_width);
		for(size_t x=0; x<m_width; x++)
		{
			//Look up the eye pattern pixel
			auto bin = eyerow[x];
			total += bin;

			//If mask pixel isn't black, count violations
			uint32_t pix = row[x];
			if( (pix & 0xff) != 0)
				hits += bin;
		}
	}

	cap->SetMaskHitRate(hits * 1.0f / total);
}
