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
#include "Waterfall.h"
#include "FFTFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

WaterfallWaveform::WaterfallWaveform(size_t width, size_t height)
	: m_width(width)
	, m_height(height)
{
	size_t npix = width*height;
	m_outdata = new float[npix];
	for(size_t i=0; i<npix; i++)
		m_outdata[i] = 0;
}

WaterfallWaveform::~WaterfallWaveform()
{
	delete[] m_outdata;
	m_outdata = NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

Waterfall::Waterfall(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_RF)
	, m_pixelsPerHz(0.001)
	, m_offsetHz(0)
	, m_width(1)
	, m_height(1)
{
	m_xAxisUnit = Unit(Unit::UNIT_HZ);

	//Set up channels
	CreateInput("Spectrum");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool Waterfall::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) &&
		(stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) &&
		(stream.m_channel->GetXAxisUnits() == Unit::UNIT_HZ)
		)
	{
		return true;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

float Waterfall::GetOffset(size_t /*stream*/)
{
	return 0;
}

float Waterfall::GetVoltageRange(size_t /*stream*/)
{
	return 1;
}

string Waterfall::GetProtocolName()
{
	return "Waterfall";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void Waterfall::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto din = GetAnalogInputWaveform(0);
	size_t inlen = din->m_samples.size();

	//Initialize the capture
	//TODO: timestamps? do we need those?
	auto cap = dynamic_cast<WaterfallWaveform*>(GetData(0));
	if(cap == NULL)
		cap = new WaterfallWaveform(m_width, m_height);
	cap->m_timescale = din->m_timescale;
	float* data = cap->GetData();

	//Move the whole waterfall down by one row
	for(size_t y=0; y < m_height-1 ; y++)
	{
		for(size_t x=0; x<m_width; x++)
			data[y*m_width + x] = data[(y+1)*m_width + x];
	}

	//Zero the new row
	float* prow = data + (m_height-1)*m_width;
	for(size_t x=0; x<m_width; x++)
		prow[x] = 0;

	//Add the new data
	double hz_per_bin = din->m_timescale;
	double bins_per_pixel = 1.0f / (m_pixelsPerHz  * hz_per_bin);
	double bin_offset = m_offsetHz / hz_per_bin;
	float vmin = 1.0 / 255.0;
	float vrange = m_inputs[0].GetVoltageRange();	//db from min to max scale
	float vfs = vrange/2 - m_inputs[0].GetOffset();
	for(size_t x=0; x<m_width; x++)
	{
		//Look up the frequency bin(s) for this position
		size_t leftbin = static_cast<size_t>(floor(bins_per_pixel*x + bin_offset));
		size_t rightbin = static_cast<size_t>(floor(bins_per_pixel*(x+1) + bin_offset));

		for(size_t nbin=leftbin; (nbin <= rightbin) && (nbin < inlen); nbin ++)
		{
			//Brightness is normalized amplitude, scaled by bins per pixel
			float v = 1 - ( (din->m_samples[nbin] - vfs) / -vrange);
			float vscale = v / bins_per_pixel;

			prow[x] += vscale;
		}

		prow[x] = max(prow[x], vmin);
	}

	SetData(cap, 0);
}
