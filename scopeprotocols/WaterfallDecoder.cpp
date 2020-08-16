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
#include "WaterfallDecoder.h"
#include "FFTDecoder.h"

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

WaterfallDecoder::WaterfallDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_RF)
	, m_pixelsPerHz(0.001)
	, m_offsetHz(0)
	, m_width(1)
	, m_height(1)
{
	m_xAxisUnit = Unit(Unit::UNIT_HZ);

	//Set up channels
	m_signalNames.push_back("din");
	m_channels.push_back(NULL);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool WaterfallDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetYAxisUnits() == Unit::UNIT_DBM) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

double WaterfallDecoder::GetOffset()
{
	return 0;
}

double WaterfallDecoder::GetVoltageRange()
{
	return 1;
}

string WaterfallDecoder::GetProtocolName()
{
	return "Waterfall";
}

bool WaterfallDecoder::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool WaterfallDecoder::NeedsConfig()
{
	//we auto-select the midpoint as our threshold
	return false;
}

void WaterfallDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "Waterfall(%s)", m_channels[0]->m_displayname.c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void WaterfallDecoder::Refresh()
{
	//Get the input data
	if(m_channels[0] == NULL)
	{
		SetData(NULL);
		return;
	}
	auto din = dynamic_cast<AnalogWaveform*>(m_channels[0]->GetData());

	//We need meaningful data
	size_t inlen = din->m_samples.size();
	if(inlen == 0)
	{
		SetData(NULL);
		return;
	}

	//Initialize the capture
	//TODO: timestamps? do we need those?
	WaterfallWaveform* cap = dynamic_cast<WaterfallWaveform*>(m_data);
	if(cap == NULL)
		cap = new WaterfallWaveform(m_width, m_height);
	cap->m_timescale = 1;
	float* data = cap->GetData();

	//Move the whole waterfall down by one row
	for(size_t y=0; y < m_height-1 ; y++)
	{
		for(size_t x=0; x<m_width; x++)
			data[y*m_width + x] = data[(y+1)*m_width + x];
	}

	//Add the new data
	double hz_per_bin = din->m_timescale;
	double bins_per_pixel = 1.0f / (m_pixelsPerHz  * hz_per_bin);
	double bin_offset = m_offsetHz / hz_per_bin;
	double vmin = 1.0 / 255.0;
	for(size_t x=0; x<m_width; x++)
	{
		//Look up the frequency bin for this position
		//For now, just do nearest neighbor interpolation
		size_t nbin = static_cast<size_t>(round(bins_per_pixel*x + bin_offset));

		float value = 0;
		if(nbin < inlen)
			value = 1 - ( (din->m_samples[nbin]) / -70 );

		//Cap values to prevent going off-scale-low with our color ramps
		if(value < vmin)
			value = vmin;

		data[(m_height-1)*m_width + x] = value;
	}

	SetData(cap);
}
