/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
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

#include "../scopehal/scopehal.h"
#include "FSKDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

FSKDecoder::FSKDecoder(const string& color)
	: Filter(color, CAT_RF)
{
	AddDigitalStream("data");
	CreateInput("Frequency");

	/*
	m_threshname = "Threshold";
	m_parameters[m_threshname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_threshname].SetFloatVal(0);

	m_hysname = "Hysteresis";
	m_parameters[m_hysname] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_hysname].SetFloatVal(0);*/
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool FSKDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) &&
		(stream.GetType() == Stream::STREAM_TYPE_ANALOG) &&
		(stream.GetYAxisUnits() == Unit(Unit::UNIT_HZ)) )
	{
		return true;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string FSKDecoder::GetProtocolName()
{
	return "FSK";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void FSKDecoder::Refresh()
{
	if(!VerifyAllInputsOKAndUniformAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(0));
	auto len = din->size();
	din->PrepareForCpuAccess();

	//Calculate min/max of the input data (ignoring really low values that failed squelch)
	float nmin = FLT_MAX;
	float nmax = -FLT_MAX;
	for(float v : din->m_samples)
	{
		if(v < 50)		//discard anything that got squelched
			continue;

		nmin = min(nmin, v);
		nmax = max(nmax, v);
	}

	//Find threshold by building a histogram of the samples
	const size_t bin_hz = 500;
	float vrange = nmax - nmin;
	const size_t nbins = ceil(vrange / bin_hz);
	auto hist = MakeHistogramClipped(din, nmin, nmax, nbins);

	//Find the highest two peaks (0 and 1 levels)
	size_t maxbin1 = 0;
	size_t maxval1 = 0;
	size_t maxbin2 = 0;
	size_t maxval2 = 0;
	size_t window = 5;
	for(size_t i=window; i<nbins-window; i++)
	{
		//Are we a local maximum?
		bool localmax = true;
		size_t cur = hist[i];
		for(size_t d=1; d<=window; d++)
		{
			if( (cur < hist[i+d]) || (cur < hist[i-d]) )
			{
				localmax = false;
				break;
			}
		}

		if(!localmax)
			continue;

		//New #1? Push us to #2
		if(hist[i] > maxval1)
		{
			maxbin2 = maxbin1;
			maxval2 = maxval1;

			maxbin1 = i;
			maxval1 = hist[i];
		}

		//New #2? Just save it
		else if(hist[i] > maxval2)
		{
			maxbin2 = i;
			maxval2 = hist[i];
		}
	}
	float freq1 = (maxbin1 * 1.0 / nbins) * vrange + nmin;
	float freq2 = (maxbin2 * 1.0 / nbins) * vrange + nmin;

	//Calculate the threshold as the midpoint between the two peaks.
	//Hysteresis is 20% of the range.
	float midpoint = (freq1 + freq2) / 2;
	float hys = fabs(freq1 - freq2) * 0.2;
	auto cap = SetupEmptyUniformDigitalOutputWaveform(din, 0);
	cap->Resize(len);
	cap->PrepareForCpuAccess();

	//Threshold all of our samples
	//Optimized inner loop if no hysteresis
	if(hys == 0)
	{
		#pragma omp parallel for
		for(size_t i=0; i<len; i++)
			cap->m_samples[i] = din->m_samples[i] > midpoint;
	}
	else
	{
		bool cur = din->m_samples[0] > midpoint;
		float thresh_rising = midpoint + hys/2;
		float thresh_falling = midpoint - hys/2;

		for(size_t i=0; i<len; i++)
		{
			float f = din->m_samples[i];
			if(cur && (f < thresh_falling))
				cur = false;
			else if(!cur && (f > thresh_rising))
				cur = true;
			cap->m_samples[i] = cur;
		}
	}

	cap->MarkModifiedFromCpu();
}
