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
#include "DifferenceDecoder.h"
#include "FFTDecoder.h"
#include <immintrin.h>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DifferenceDecoder::DifferenceDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MATH)
{
	//Set up channels
	m_signalNames.push_back("IN+");
	m_signalNames.push_back("IN-");
	m_channels.push_back(NULL);
	m_channels.push_back(NULL);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DifferenceDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	if( (i == 1) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void DifferenceDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "%s - %s", m_channels[0]->m_displayname.c_str(), m_channels[1]->m_displayname.c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string DifferenceDecoder::GetProtocolName()
{
	return "Subtract";
}

bool DifferenceDecoder::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool DifferenceDecoder::NeedsConfig()
{
	//we have more than one input
	return true;
}

double DifferenceDecoder::GetVoltageRange()
{
	//TODO: default, but allow overriding
	double v1 = m_channels[0]->GetVoltageRange();
	double v2 = m_channels[1]->GetVoltageRange();
	return max(v1, v2) * 2;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DifferenceDecoder::Refresh()
{
	//Get the input data
	if( (m_channels[0] == NULL) || (m_channels[1] == NULL) )
	{
		SetData(NULL);
		return;
	}
	auto din_p = dynamic_cast<AnalogWaveform*>(m_channels[0]->GetData());
	auto din_n = dynamic_cast<AnalogWaveform*>(m_channels[1]->GetData());
	if( (din_p == NULL) || (din_n == NULL) )
	{
		SetData(NULL);
		return;
	}

	//Set up units and complain if they're inconsistent
	m_xAxisUnit = m_channels[0]->GetXAxisUnits();
	m_yAxisUnit = m_channels[0]->GetYAxisUnits();
	if( (m_xAxisUnit != m_channels[1]->GetXAxisUnits()) || (m_yAxisUnit != m_channels[1]->GetYAxisUnits()) )
	{
		SetData(NULL);
		return;
	}

	//We need meaningful data
	size_t len = din_p->m_samples.size();
	if(din_n->m_samples.size() < len)
		len = din_n->m_samples.size();
	if(len == 0)
	{
		SetData(NULL);
		return;
	}

	//Create the output and copy timestamps
	AnalogWaveform* cap = new AnalogWaveform;
	cap->Resize(len);
	cap->CopyTimestamps(din_p);
	float* out = (float*)&cap->m_samples[0];
	float* a = (float*)&din_p->m_samples[0];
	float* b = (float*)&din_n->m_samples[0];

	//Do the actual subtraction
	if(g_hasAvx2)
		InnerLoopAVX2(out, a, b, len);
	else
		InnerLoop(out, a, b, len);

	SetData(cap);

	//Copy our time scales from the input
	//Use the first trace's timestamp as our start time if they differ
	cap->m_timescale 		= din_p->m_timescale;
	cap->m_startTimestamp 	= din_p->m_startTimestamp;
	cap->m_startPicoseconds = din_p->m_startPicoseconds;
}

//We probably still have SSE2 or similar if no AVX, so give alignment hints for compiler auto-vectorization
void DifferenceDecoder::InnerLoop(float* out, float* a, float* b, size_t len)
{
	out = (float*)__builtin_assume_aligned(out, 64);
	a = (float*)__builtin_assume_aligned(a, 64);
	b = (float*)__builtin_assume_aligned(b, 64);

	for(size_t i=0; i<len; i++)
		out[i] 		= a[i] - b[i];
}

__attribute__((target("avx2")))
void DifferenceDecoder::InnerLoopAVX2(float* out, float* a, float* b, size_t len)
{
	size_t end = len - (len % 8);

	//AVX2
	for(size_t i=0; i<end; i+=8)
	{
		__m256 pa = _mm256_load_ps(a + i);
		__m256 pb = _mm256_load_ps(b + i);
		__m256 o = _mm256_sub_ps(pa, pb);
		_mm256_store_ps(out+i, o);
	}

	//Get any extras
	for(size_t i=end; i<len; i++)
		out[i] 		= a[i] - b[i];
}
