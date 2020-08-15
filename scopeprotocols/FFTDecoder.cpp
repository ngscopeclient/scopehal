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
#include "FFTDecoder.h"
#include <ffts.h>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

FFTDecoder::FFTDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_RF)
{
	m_xAxisUnit = Unit(Unit::UNIT_HZ);
	m_yAxisUnit = Unit(Unit::UNIT_DB);

	//Set up channels
	m_signalNames.push_back("din");
	m_channels.push_back(NULL);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool FFTDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

double FFTDecoder::GetOffset()
{
	return 0;
}

double FFTDecoder::GetVoltageRange()
{
	return 1;//m_channels[0]->GetVoltageRange();
}

string FFTDecoder::GetProtocolName()
{
	return "FFT";
}

bool FFTDecoder::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool FFTDecoder::NeedsConfig()
{
	//we auto-select the midpoint as our threshold
	return false;
}

void FFTDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "FFT(%s)", m_channels[0]->m_displayname.c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void FFTDecoder::Refresh()
{
	//Get the input data
	if(m_channels[0] == NULL)
	{
		SetData(NULL);
		return;
	}
	AnalogWaveform* din = dynamic_cast<AnalogWaveform*>(m_channels[0]->GetData());
	if(din == NULL)
	{
		SetData(NULL);
		return;
	}

	//We need meaningful data
	const size_t npoints_raw = din->m_samples.size();
	if(npoints_raw == 0)
	{
		SetData(NULL);
		return;
	}

	//Truncate to next power of 2 down
	const size_t npoints = pow(2,floor(log2(npoints_raw)));
	LogTrace("FFTDecoder: processing %zu raw points\n", npoints_raw);
	LogTrace("Rounded to %zu\n", npoints);

	//Format the input data as raw samples for the FFT
	//TODO: handle non-uniform sample rates
	float* rdin;
	size_t insize = npoints * sizeof(float);

#ifdef _WIN32
	rdin = (float*)_aligned_malloc(insize, 32);
#else
	posix_memalign((void**)&rdin, 32, insize);
#endif

	memcpy(rdin, &din->m_samples[0], insize);

	float* rdout;
	const size_t nouts = npoints/2 + 1;

#ifdef _WIN32
	rdout = (float*)_aligned_malloc(2 * nouts * sizeof(float), 32);
#else
	posix_memalign((void**)&rdout, 32, 2 * nouts * sizeof(float));
#endif

	//Calculate the FFT
	auto plan = ffts_init_1d_real(npoints, FFTS_FORWARD);
	ffts_execute(plan, &rdin[0], &rdout[0]);
	ffts_free(plan);

	//Set up output and copy timestamps
	auto cap = new AnalogWaveform;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startPicoseconds = din->m_startPicoseconds;

	//Calculate size of each bin
	double ps = din->m_timescale * (din->m_offsets[1] - din->m_offsets[0]);
	double sample_ghz = 1000 / ps;
	double bin_hz = round((0.5f * sample_ghz * 1e9f) / nouts);
	cap->m_timescale = bin_hz;

	//Normalize magnitudes
	vector<float> mags;
	float maxmag = 1;
	for(size_t i=1; i<nouts; i++)	//don't print (DC offset?) term 0
									//real fft has symmetric output, ignore the redundant image of the data
	{
		float a = rdout[i*2];
		float b = rdout[i*2 + 1];
		float mag = sqrtf(a*a + b*b);
		//float freq = (0.5f * i * sample_ghz * 1000) / nouts;

		mags.push_back(mag);
		if(mag > maxmag)
			maxmag = mag;
	}

	cap->Resize(nouts);
	cap->m_offsets[0] = 0;
	cap->m_durations[0] = 1;
	cap->m_samples[0] = 1;
	for(size_t i=1; i<nouts; i++)
	{
		cap->m_offsets[i] = i;
		cap->m_durations[i] = 1;
		cap->m_samples[i] = mags[i-1] / maxmag;
	}

	SetData(cap);

	//Clean up
#ifdef _WIN32
	_aligned_free(rdin);
	_aligned_free(rdout);
#else
	free(rdin);
	free(rdout);
#endif
}
