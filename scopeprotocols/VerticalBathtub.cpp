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

#include "scopeprotocols.h"
#include "VerticalBathtub.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

VerticalBathtub::VerticalBathtub(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_ANALYSIS)
{
	m_xAxisUnit = Unit(Unit::UNIT_MILLIVOLTS);
	SetYAxisUnits(Unit(Unit::UNIT_LOG_BER), 0);

	//Set up channels
	CreateInput("din");

	m_timeName = "Time";
	m_parameters[m_timeName] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));
	m_parameters[m_timeName].SetFloatVal(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool VerticalBathtub::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_EYE) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string VerticalBathtub::GetProtocolName()
{
	return "Vert Bathtub";
}

bool VerticalBathtub::NeedsConfig()
{
	return true;
}

float VerticalBathtub::GetVoltageRange(size_t /*stream*/)
{
	//1e12 total height
	return 12;
}

float VerticalBathtub::GetOffset(size_t /*stream*/)
{
	//1e-6 is the midpoint
	return 6;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void VerticalBathtub::Refresh()
{
	if(!VerifyAllInputsOK(true))
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto eye = dynamic_cast<EyeWaveform*>(GetInputWaveform(0));
	int64_t timestamp = m_parameters[m_timeName].GetIntVal();

	//Find the eye bin for this column
	double fs_per_width = 2*eye->m_uiWidth;
	double fs_per_pixel = fs_per_width / eye->GetWidth();
	size_t xbin = round( (timestamp + eye->m_uiWidth) / fs_per_pixel );

	//Sanity check we're not off the eye
	if(xbin >= eye->GetWidth())
		return;

	//Create the output
	auto cap = new AnalogWaveform;
	cap->m_timescale = eye->m_timescale;
	cap->m_startTimestamp = eye->m_startTimestamp;
	cap->m_startFemtoseconds = eye->m_startFemtoseconds;
	cap->m_triggerPhase = 0;

	//Eye height config
	auto range = GetInput(0).GetVoltageRange();
	double mv_per_pixel = 1000 * range / eye->GetHeight();
	double mv_off = 1000 * (range/2 - eye->GetCenterVoltage());

	//Extract the single column we're interested in
	//TODO: support a range of times around the midpoint
	int64_t* data = eye->GetAccumData();
	size_t width = eye->GetWidth();
	size_t len = eye->GetHeight();
	cap->Resize(len);
	for(size_t i=0; i<len; i++)
	{
		cap->m_offsets[i] = i*mv_per_pixel - mv_off;
		cap->m_durations[i] = mv_per_pixel;
		cap->m_samples[i] = data[i*width + xbin];
	}
	SetData(cap, 0);

	//Move from the center out and integrate BER
	float sumleft = 0;
	float sumright = 0;
	ssize_t mid = len/2;
	for(ssize_t i=mid; i>=0; i--)
	{
		float& samp = cap->m_samples[i];
		sumleft += samp;
		samp = sumleft;
	}
	for(size_t i=mid; i<len; i++)
	{
		float& samp = cap->m_samples[i];
		sumright += samp;
		samp = sumright;
	}

	//Normalize to max amplitude
	float nmax = sumleft;
	if(sumright > sumleft)
		nmax = sumright;
	for(size_t i=0; i<len; i++)
		cap->m_samples[i] /= nmax;

	//Log post-scaling
	for(size_t i=0; i<len; i++)
	{
		float& samp = cap->m_samples[i];
		if(samp < 1e-12)
			samp = -14;	//cap ber if we don't have enough data
		else
			samp = log10(samp);
	}
}
