/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2017 Andrew D. Zonenberg                                                                          *
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
#include "EyeDecoder.h"
#include "EyeRenderer.h"
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

EyeDecoder::EyeDecoder(
	std::string hwname, std::string color)
	: ProtocolDecoder(hwname, OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color)
{
	//Set up channels
	m_signalNames.push_back("din");
	m_channels.push_back(NULL);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

ChannelRenderer* EyeDecoder::CreateRenderer()
{
	return new EyeRenderer(this);
}

bool EyeDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string EyeDecoder::GetProtocolName()
{
	return "Eye pattern";
}

bool EyeDecoder::IsOverlay()
{
	return false;
}

bool EyeDecoder::NeedsConfig()
{
	//TODO: make this true, trigger needs config
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

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
	int neighborhood = 60;
	int64_t maxpeak = 0;
	for(auto it : vhist)
	{
		if(it.second > maxpeak)
			maxpeak = it.second;
	}
	LogDebug("Highest histogram peak is %ld points\n", maxpeak);

	int64_t peakthresh = maxpeak/4;
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
	/*LogDebug("    Decision points:\n");
	for(auto v : cap->m_decisionPoints)
		LogDebug("        %6.3f V\n", v);*/

	//Sanity check
	if(cap->m_signalLevels.size() < 2)
	{
		LogDebug("Couldn't find at least two distinct symbol voltages\n");
		delete cap;
		return false;
	}

	return true;
}

bool EyeDecoder::CalculateUIWidth(AnalogCapture* din, EyeCapture* cap)
{
	//Calculate an initial guess of the UI by triggering at the start of every bit
	float last_sample_value = 0;
	int64_t tstart = 0;
	vector<int64_t> ui_widths;
	for(auto sin : din->m_samples)
	{
		float f = sin;
		int64_t old_tstart = tstart;

		//Dual-edge trigger, no holdoff
		for(auto v : cap->m_decisionPoints)
		{
			if( (f > v) && (last_sample_value < v) )
				tstart = sin.m_offset;
			if( (f < v) && (last_sample_value > v) )
				tstart = sin.m_offset;
		}
		last_sample_value = f;

		//If we triggered this cycle, add the delta
		//Don't count the first partial UI
		if( (tstart != old_tstart) && (old_tstart != 0) )
			ui_widths.push_back(tstart - old_tstart);
	}

	//Figure out the best guess width of the unit interval
	//We should never trigger more than once in a UI, but we might have several UIs between triggers
	//Compute a histogram of the UI widths and pick the highest bin. This is probably one UI.
	map<int, int64_t> hist;
	for(auto w : ui_widths)
		hist[w] ++;
	int max_bin = 0;
	int64_t max_count = 0;
	for(auto it : hist)
	{
		if(it.second > max_count)
		{
			max_count = it.second;
			max_bin = it.first;
		}
	}

	int64_t eye_width = max_bin;
	double baud = 1e6 / (eye_width * cap->m_timescale);
	LogDebug("Computing symbol rate\n");
	LogDebug("    UI width (first pass): %ld samples / %.3f ns (%.3lf Mbd)\n",
		eye_width, eye_width * cap->m_timescale / 1e3, baud);

	//Second pass: compute weighted average around that point.
	//We may have some variation in UI width due to ISI
	int64_t bin_sum = 0;
	int64_t bin_count = 0;
	int range = 0.45*eye_width;	//narrow enough to avoid harmonics of UI, but pretty wide otherwise
	for(int delta = -range; delta <= range; delta++)
	{
		int bin = eye_width + delta;
		int64_t count = hist[bin];
		bin_count += count;
		bin_sum += count*bin;
	}
	double weighted_width = bin_sum * 1.0 / bin_count;
	eye_width = weighted_width;
	baud = 1e6 / (eye_width * cap->m_timescale);
	LogDebug("    UI width (second pass, window=%d to %d): %ld samples / %.3f ns (%.3lf Mbd)\n",
		max_bin-range, max_bin+range, eye_width, eye_width * cap->m_timescale / 1e3, baud);

	//Third pass: compute the sum of UIs across the entire signal and average.
	//If the delta is significantly off from our first-guess UI, call it two!
	last_sample_value = 0;
	tstart = 0;
	int64_t ui_width_sum = 0;
	int64_t ui_width_count = 0;
	for(auto sin : din->m_samples)
	{
		float f = sin;
		int64_t old_tstart = tstart;

		//Dual-edge trigger, no holdoff
		for(auto v : cap->m_decisionPoints)
		{
			if( (f > v) && (last_sample_value < v) )
				tstart = sin.m_offset;
			if( (f < v) && (last_sample_value > v) )
				tstart = sin.m_offset;
		}
		last_sample_value = f;

		//If we triggered this cycle, add the delta
		//Don't count the first partial UI
		if( (tstart != old_tstart) && (old_tstart != 0) )
		{
			int64_t w = tstart - old_tstart;

			//Skip runt pulses (glitch?)
			if(w < eye_width/2)
				continue;

			//If it's more than 1.5x the first-guess UI, estimate the width and add it
			if(w > eye_width * 1.5f)
			{
				//Don't try guessing runs more than 6 UIs long, too inaccurate.
				//Within each guess allow +/- 25% variance for the actual edge location.
				for(int guess=2; guess<=6; guess++)
				{
					float center = guess * eye_width;
					float low = center - eye_width*0.25;
					float high = center + eye_width*0.25;
					if( (w > low) && (w < high) )
					{
						ui_width_sum += w;
						ui_width_count += guess;
						break;
					}
				}
				continue;
			}

			//It looks like a single UI! Count it
			ui_width_sum += w;
			ui_width_count ++;
		}
	}

	double average_width = ui_width_sum * 1.0 / ui_width_count;
	//LogDebug("    Average UI width (second pass): %.3lf samples\n", average_width);
	m_uiWidth = round(average_width);
	m_uiWidthFractional = average_width;

	baud = 1e6f / (eye_width * cap->m_timescale);
	LogDebug("    UI width (third pass): %ld samples / %.3f ns (%.3lf Mbd)\n",
		eye_width, eye_width * cap->m_timescale / 1e3, baud);

	//Sanity check
	if(eye_width == 0)
	{
		LogDebug("No trigger found\n");
		delete cap;
		return false;
	}

	return true;
}

bool EyeDecoder::MeasureEyeOpenings(EyeCapture* cap, map<int64_t, map<float, int64_t> >& pixmap)
{
	//Measure the width of the eye at each decision point
	//LogDebug("Measuring eye width\n");
	float row_height = 0.01;				//sample +/- 10 mV around the decision point
	for(auto v : cap->m_decisionPoints)
	{
		//Initialize the row
		vector<int64_t> row;
		for(int i=0; i<m_uiWidth; i++)
			row.push_back(0);

		//Search this band and see where we have signal
		for(auto it : pixmap)
		{
			int64_t time = it.first;
			for(auto jt : it.second)
			{
				if(fabs(jt.first - v) > row_height)
					continue;
				row[time] += jt.second;
			}
		}

		//Start from the middle and look left and right
		int middle = m_uiWidth/2;
		int left = middle;
		int right = middle;
		for(; left > 0; left--)
		{
			if(row[left-1] != 0)
				break;
		}
		for(; right < m_uiWidth-1; right++)
		{
			if(row[right+1] != 0)
				break;
		}

		int width = right-left;
		/*
		LogDebug("    At %.3f V: left=%d, right=%d, width=%d (%.3f ns, %.2f UI)\n",
			v,
			left,
			right,
			width,
			width * cap->m_timescale / 1e3,
			width * 1.0f / eye_width
			);*/
		cap->m_eyeWidths.push_back(width);
	}

	//Find where we have signal right around the middle of the eye
	int64_t col_width = 1;					//sample +/- 1 sample around the center of the opening
	//LogDebug("Measuring eye height\n");
	map<int, int64_t> colmap;
	vector<int> voltages;
	int64_t target = m_uiWidth/2;
	for(auto it : pixmap)
	{
		int64_t time = it.first;
		if( ( (time - target) > col_width ) || ( (time - target) < -col_width ) )
			continue;

		for(auto jt : it.second)
		{
			float mv = jt.first * 1000;
			voltages.push_back(mv);
			colmap[mv] = jt.second;
		}
	}
	sort(voltages.begin(), voltages.end());
	//for(auto y : voltages)
	//	LogDebug("    %.3f: %lu\n", y*0.001f, colmap[y]);

	//Search around each eye opening and find the available space
	for(auto middle : cap->m_decisionPoints)
	{
		float vmin = -999;
		float vmax = 999;

		for(auto v : voltages)
		{
			float fv = v * 0.001f;

			if(fv < middle)
			{
				if(fv > vmin)
					vmin = fv;
			}
			else if(fv < vmax)
				vmax = fv;
		}
		float height = vmax - vmin;
		cap->m_eyeHeights.push_back(height);

		/*LogDebug("    At %.3f V: [%.3f, %.3f], height = %.3f\n",
			middle, vmin, vmax, height);*/
	}

	return true;
}

bool EyeDecoder::GenerateEyeData(AnalogCapture* din, EyeCapture* cap, map<int64_t, map<float, int64_t> >& pixmap)
{
	//Generate the final pixel map
	bool first = true;
	float last_sample_value = 0;
	int64_t tstart = 0;
	int64_t uis_per_trigger = 16;		//TODO: allow changing this?
	for(auto sin : din->m_samples)
	{
		float f = sin;

		//If we haven't triggered, wait for the signal to cross a decision threshold
		//so we can phase align to the data clock.
		if(tstart == 0)
		{
			if(!first)
			{
				for(auto v : cap->m_decisionPoints)
				{
					if( (f > v) && (last_sample_value < v) )
						tstart = sin.m_offset;
					if( (f < v) && (last_sample_value > v) )
						tstart = sin.m_offset;
				}
			}
			else
				first = false;

			last_sample_value = f;
			continue;
		}

		//If we get here, we've triggered. Chop the signal at UI boundaries
		double doff = sin.m_offset - tstart;
		int64_t offset = round(fmod(doff, m_uiWidthFractional));
		if(offset >= m_uiWidth)
			offset = 0;

		//and add to the histogram
		pixmap[offset][f] ++;

		//Re-trigger every uis_per_trigger UIs to compensate for clock skew between our guesstimated clock
		//and the actual line rate
		//TODO: proper CDR PLL for this
		double num_uis = doff / m_uiWidthFractional;
		if(num_uis > uis_per_trigger)
		{
			tstart = 0;
			first = true;
		}
	}

	//Generate the samples
	for(auto it : pixmap)
	{
		for(auto jt : it.second)
		{
			//For now just add a sample for it
			EyePatternPixel pix;
			pix.m_voltage = jt.first;
			pix.m_count = jt.second;
			cap->m_samples.push_back(EyeSample(it.first, 1, pix));
		}
	}

	return true;
}

int EyeDecoder::GetCodeForVoltage(float v, EyeCapture* cap)
{
	int best_sample = 0;
	float best_delta = 999;
	for(int i=0; i<(int)cap->m_signalLevels.size(); i++)
	{
		float dv = fabs(cap->m_signalLevels[i] - v);
		if(dv < best_delta)
		{
			best_delta = dv;
			best_sample = i;
		}
	}
	return best_sample;
}

bool EyeDecoder::MeasureRiseFallTimes(AnalogCapture* din, EyeCapture* cap)
{
	//Minimum slew rate (hitting this is considered the end of a transition)
	double minSeparation = fabs(cap->m_signalLevels[1] - cap->m_signalLevels[0]);
	double minSlew = minSeparation / (3*m_uiWidth);
	//LogDebug("Min separation: %.0f mV\n", minSeparation * 1000);
	//LogDebug("Min slew: %.0f mV/sample\n", minSlew * 1000);

	//Find the set of transitions we support.
	//Map from (src_code, dest_code) to vector<midpoint>
	//Note that not all line codes use all transitions, for example MLT-3 has no -1 to +1
	typedef pair<int, int> ipair;
	map< ipair, vector<int64_t> > transitionsObserved;
	float last_sample_value = 0;
	//LogDebug("Finding legal transitions in signal...\n");
	for(int64_t i=0; i<(int64_t)din->m_samples.size(); i++)
	{
		auto samp = din->m_samples[i];
		float f = samp;

		//See if this is a rising or falling edge
		bool is_transition = false;
		for(auto v : cap->m_decisionPoints)
		{
			if( (f > v) && (last_sample_value < v) )
				is_transition = true;
			if( (f < v) && (last_sample_value > v) )
				is_transition = true;
		}
		last_sample_value = f;

		//Skip anything that isn't the midpoint of a transition
		if(!is_transition || i <= 2)
			continue;

		//We found a transition! Search left to find the starting code value.
		//Stop after half a UI, or when we level off
		//float starting_voltage = f;
		float oldVoltage = f;
		size_t halfWidth = m_uiWidth / 2;
		//size_t start_delay = 1;
		for(size_t j=i-1; j>0 && (i-j)<halfWidth; j--)
		{
			auto s = din->m_samples[j];
			float g = s;
			//starting_voltage = g;

			//If we're not slewing much (more than 1 level per 3 UIs), stop
			float dv = fabs(g - oldVoltage);
			if(dv < minSlew)
				break;

			oldVoltage = g;
			//start_delay = i-j;
		}

		//See what the old state is
		int old_code = GetCodeForVoltage(oldVoltage, cap);
		//LogDebug("Old voltage: %d, %3.0f mV (%2zu cycles before midpoint)\n",
		//	old_code, starting_voltage * 1000, start_delay);

		//Repeat to the right to find the ending code value
		//float ending_voltage = f;
		float newVoltage = f;
		//size_t end_delay = 1;
		for(size_t j=i+1; j<din->m_samples.size() && (j-i)<halfWidth; j++)
		{
			auto s = din->m_samples[j];
			float g = s;
			//ending_voltage = g;

			//If we're not slewing much (more than 1 level per 3 UIs), stop
			float dv = fabs(g - newVoltage);
			if(dv < minSlew)
				break;

			newVoltage = g;
			//end_delay = j-i;
		}

		int new_code = GetCodeForVoltage(newVoltage, cap);
		//LogDebug("New voltage: %d, %3.0f mV (%2zu cycles after midpoint)\n",
		//	new_code, ending_voltage * 1000, end_delay);

		//Save this transition
		transitionsObserved[ipair(old_code, new_code)].push_back(i);
	}

	//DEBUG: Print out the set of transitions we saw
	/*
	for(auto it : transitionsObserved)
	{
		LogDebug("%d -> %d: %lu occurrences\n",
			it.first.first,
			it.first.second,
			it.second.size());
	}
	*/

	//Once we know what the legal transitions are, examine every occurrence of each.
	//Find the rise or fall time (10-90% for now)
	for(auto it : transitionsObserved)
	{
		float originalVoltage = cap->m_signalLevels[it.first.first];
		float endingVoltage = cap->m_signalLevels[it.first.second];
		float dv = endingVoltage - originalVoltage;
		float startThreshold = originalVoltage + dv*0.1;
		float endThreshold = endingVoltage - dv*0.1;

		LogDebug("Code %d->%d: startThreshold=%3.0f mV, endThreshold=%3.0f mV\n",
			it.first.first,
			it.first.second,
			startThreshold * 1000,
			endThreshold * 1000);

		int64_t timeSum = 0;
		int64_t timeCount = 0;
		for(auto i : it.second)
		{
			float midpoint = din->m_samples[i];

			//Go back until we cross the 10% threshold
			int startDelay = 0;
			for(size_t j=i-1; j>0; j--)
			{
				auto s = din->m_samples[j];
				float v = s;

				if(fabs(v - midpoint) > fabs(startThreshold - midpoint) )
				{
					startDelay = i-j;
					break;
				}
			}

			//Go forward until we cross the 90% threshold
			int endDelay = 0;
			for(size_t j=i+1; j<din->m_samples.size(); j++)
			{
				auto s = din->m_samples[j];
				float v = s;

				if(fabs(v - midpoint) > fabs(endThreshold - midpoint) )
				{
					endDelay = j-i;
					break;
				}
			}

			int edgetime = endDelay + startDelay;

			//If the edge is more than 3/4 a UI long, discount it.
			//We probably have two high/low bits in a row.
			if( edgetime > (3*m_uiWidth / 4) )
				continue;

			//LogDebug("    Edge = %d samples (%.2f ns)\n", edgetime, edgetime * cap->m_timescale * 1e-3f);

			timeSum += edgetime;
			timeCount ++;
		}

		//Calculate the average rise/fall time
		double averageTime = timeSum * 1.0 / timeCount;
		//LogDebug("Average time: %.3lf ns\n", averageTime * cap->m_timescale * 1e-3);

		//LogDebug("    Start delay = %d samples (%.2f ns)\n", startDelay, startDelay * cap->m_timescale * 1e-3f);

		cap->m_riseFallTimes[it.first] = averageTime;
	}

	return true;
}

void EyeDecoder::Refresh()
{
	LogIndenter li;

	//Get the input data
	if(m_channels[0] == NULL)
	{
		SetData(NULL);
		return;
	}
	AnalogCapture* din = dynamic_cast<AnalogCapture*>(m_channels[0]->GetData());
	if(din == NULL)
	{
		SetData(NULL);
		return;
	}

	//Can't do much if we have no samples to work with
	if(din->GetDepth() == 0)
	{
		SetData(NULL);
		return;
	}

	//Initialize the capture
	EyeCapture* cap = new EyeCapture;
	m_timescale = m_channels[0]->m_timescale;
	cap->m_timescale = din->m_timescale;
	cap->m_sampleCount = din->m_samples.size();

	//Figure out what modulation is in use and what the levels are
	if(!DetectModulationLevels(din, cap))
		return;

	//Once we have decision thresholds, we can find bit boundaries and calculate the symbol rate
	if(!CalculateUIWidth(din, cap))
		return;

	//Create the actual 2D eye render
	map<int64_t, map<float, int64_t> > pixmap;
	if(!GenerateEyeData(din, cap, pixmap))
		return;

	//Find the X/Y size of each eye opening
	if(!MeasureEyeOpenings(cap, pixmap))
		return;

	//Measure our rise-fall times
	if(!MeasureRiseFallTimes(din, cap))
		return;

	//Done, update the waveform
	SetData(cap);
}
