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
#include "DownsampleDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DownsampleDecoder::DownsampleDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MATH)
{
	//Set up channels
	m_signalNames.push_back("RF");
	m_channels.push_back(NULL);

	m_factorname = "Downsample Factor";
	m_parameters[m_factorname] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_INT);
	m_parameters[m_factorname].SetIntVal(10);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DownsampleDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

double DownsampleDecoder::GetVoltageRange()
{
	return m_channels[0]->GetVoltageRange();
}

string DownsampleDecoder::GetProtocolName()
{
	return "Downsample";
}

bool DownsampleDecoder::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool DownsampleDecoder::NeedsConfig()
{
	return true;
}

void DownsampleDecoder::SetDefaultName()
{
	char hwname[256];
	Unit hz(Unit::UNIT_HZ);
	snprintf(hwname, sizeof(hwname), "Downsample(%s, %ld)",
		m_channels[0]->m_displayname.c_str(),
		m_parameters[m_factorname].GetIntVal());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DownsampleDecoder::Refresh()
{
	//Get the input data
	if(m_channels[0] == NULL)
	{
		SetData(NULL);
		return;
	}
	auto din = dynamic_cast<AnalogWaveform*>(m_channels[0]->GetData());
	if(din == NULL)
	{
		SetData(NULL);
		return;
	}

	//We need meaningful data
	size_t len = din->m_samples.size();
	if(len == 0)
	{
		SetData(NULL);
		return;
	}

	//Cut off all frequencies shorter than 1.5x our decimation factor
	int64_t factor = m_parameters[m_factorname].GetIntVal();
	size_t outlen = len / factor;
	float cutoff_period = factor * 1.5;
	float sigma = cutoff_period / sqrt(2 * log(2));
	int kernel_radius = ceil(3*sigma);

	//Generate the actual Gaussian kernel
	int kernel_size = kernel_radius*2 + 1;
	vector<float> kernel;
	kernel.resize(kernel_size);
	float alpha = 1.0f / (sigma * sqrt(2*M_PI));
	for(int x=0; x < kernel_size; x++)
	{
		int delta = (x - kernel_radius);
		kernel[x] = alpha * exp(-delta*delta/(2*sigma));
	}
	float sum = 0;
	for(auto k : kernel)
		sum += k;
	for(int i=0; i<kernel_size; i++)
		kernel[i] /= sum;

	//Do the actual downsampling.
	//For now, assume uniform sample rate
	auto cap = new AnalogWaveform;
	cap->Resize(outlen);
	for(size_t i=0; i<outlen; i++)
	{
		//Copy timestamps
		cap->m_offsets[i]	= din->m_offsets[i*factor] / factor;
		cap->m_durations[i]	= din->m_durations[i*factor] / factor;

		//Do the convolution
		float conv = 0;
		ssize_t base = i*factor;
		for(ssize_t delta = -kernel_radius; delta <= kernel_radius; delta ++)
		{
			ssize_t pos = base + delta;
			if( (pos < 0) || (pos >= (ssize_t)len) )
				continue;

			conv += din->m_samples[pos] * kernel[delta + kernel_radius];
		}

		//Do the actual decimation
		cap->m_samples[i] 	= conv;
	}
	SetData(cap);

	//Copy our time scales from the input
	cap->m_timescale = din->m_timescale * factor;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startPicoseconds = din->m_startPicoseconds;
}
