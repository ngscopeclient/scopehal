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
#include "DeEmbedDecoder.h"
#include <ffts.h>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DeEmbedDecoder::DeEmbedDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_ANALYSIS)
{
	//Set up channels
	m_signalNames.push_back("din");
	m_channels.push_back(NULL);

	m_fname = "S-Parameters";
	m_parameters[m_fname] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_FILENAMES);
	m_parameters[m_fname].m_fileFilterMask = "*.s2p";
	m_parameters[m_fname].m_fileFilterName = "Touchstone S-parameter files (*.s2p)";

	m_range = 1;
	m_offset = 0;
	m_min = FLT_MAX;
	m_max = -FLT_MAX;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DeEmbedDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

double DeEmbedDecoder::GetVoltageRange()
{
	return m_range;
}

double DeEmbedDecoder::GetOffset()
{
	return m_offset;
}

string DeEmbedDecoder::GetProtocolName()
{
	return "De-Embed";
}

bool DeEmbedDecoder::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool DeEmbedDecoder::NeedsConfig()
{
	//we need the offset to be specified, duh
	return true;
}

void DeEmbedDecoder::SetDefaultName()
{
	vector<string> fnames = m_parameters[m_fname].GetFileNames();
	string base;
	for(auto f : fnames)
	{
		if(base != "")
			base += ", ";
		base += basename(f.c_str());
	}

	char hwname[256];
	snprintf(
		hwname,
		sizeof(hwname),
		"DeEmbed(%s, %s)",
		m_channels[0]->m_displayname.c_str(),
		base.c_str()
		);

	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DeEmbedDecoder::Refresh()
{
	DoRefresh(true);
}

void DeEmbedDecoder::ClearSweeps()
{
	m_range = 1;
	m_offset = 0;
	m_min = FLT_MAX;
	m_max = -FLT_MAX;
}

/**
	@brief Applies the S-parameters in the forward or reverse direction
 */
void DeEmbedDecoder::DoRefresh(bool invert)
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

	//Reload the S-parameters from the Touchstone file(s) if the filename has changed
	vector<string> fnames = m_parameters[m_fname].GetFileNames();
	if(fnames != m_cachedFileNames)
	{
		m_cachedFileNames = fnames;

		m_sparams.Clear();
		for(auto f : fnames)
			m_sparams *= TouchstoneParser(f);
	}

	//TODO: optimization, resample s-parameters to our sample rate once vs every waveform update

	//We need meaningful data
	const size_t npoints_raw = din->m_samples.size();
	if(npoints_raw == 0)
	{
		SetData(NULL);
		return;
	}

	//Truncate to next power of 2 down
	const size_t npoints = pow(2,floor(log2(npoints_raw)));
	//LogTrace("DeEmbedDecoder: processing %zu raw points\n", npoints_raw);
	//LogTrace("Rounded to %zu\n", npoints);

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

	//Set up the FFT
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

	//Calculate size of each bin
	double ps = din->m_timescale * (din->m_offsets[1] - din->m_offsets[0]);
	double sample_ghz = 1000 / ps;
	double bin_hz = round((0.5f * sample_ghz * 1e9f) / nouts);

	//Do the actual de-embed
	if(invert)
	{
		for(size_t i=0; i<nouts; i++)
		{
			//Resample the S-parameter file for our point
			float freq = bin_hz * i;
			auto point = m_sparams.SamplePoint(2, 1, freq);

			//Zero channel response = flatten
			if(fabs(point.m_amplitude) < FLT_EPSILON)
			{
				rdout[i*2 + 0] = 0;
				rdout[i*2 + 1] = 0;
				continue;
			}

			float cosval = cos(-point.m_phase);
			float sinval = sin(-point.m_phase);

			//Uncorrected complex value
			float real_orig = rdout[i*2 + 0];
			float imag_orig = rdout[i*2 + 1];

			//Phase correction
			float real = real_orig*cosval - imag_orig*sinval;
			float imag = real_orig*sinval + imag_orig*cosval;

			//Amplitude correction
			rdout[i*2 + 0] = real / point.m_amplitude;
			rdout[i*2 + 1] = imag / point.m_amplitude;
		}
	}

	//Forward channel emulation
	else
	{
		for(size_t i=0; i<nouts; i++)
		{
			//Resample the S-parameter file for our point
			float freq = bin_hz * i;
			auto point = m_sparams.SamplePoint(2, 1, freq);

			//Zero channel response = flatten
			if(fabs(point.m_amplitude) < FLT_EPSILON)
			{
				rdout[i*2 + 0] = 0;
				rdout[i*2 + 1] = 0;
				continue;
			}

			float cosval = cos(point.m_phase);
			float sinval = sin(point.m_phase);

			//Uncorrected complex value
			float real_orig = rdout[i*2 + 0];
			float imag_orig = rdout[i*2 + 1];

			//Phase correction
			float real = real_orig*cosval - imag_orig*sinval;
			float imag = real_orig*sinval + imag_orig*cosval;

			//Amplitude correction
			rdout[i*2 + 0] = real * point.m_amplitude;
			rdout[i*2 + 1] = imag * point.m_amplitude;
		}
	}

	//Set up the inverse FFT
	float* ddout;
#ifdef _WIN32
	ddout = (float*)_aligned_malloc(insize * sizeof(float), 32);
#else
	posix_memalign((void**)&ddout, 32, insize * sizeof(float));
#endif

	//Calculate the inverse FFT
	plan = ffts_init_1d_real(npoints, FFTS_BACKWARD);
	ffts_execute(plan, &rdout[0], &ddout[0]);
	ffts_free(plan);

	//Calculate maximum group delay for the first few S-parameter bins (approx propagation delay of the channel)
	auto& s21 = m_sparams[SPair(2,1)];
	float max_delay = 0;
	for(size_t i=0; i<s21.size()-1 && i<50; i++)
		max_delay = max(max_delay, s21.GetGroupDelay(i));
	int64_t groupdelay_samples = ceil( (max_delay * 1e12) / din->m_timescale);

	//Set up output and copy timestamps
	auto cap = new AnalogWaveform;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startPicoseconds = din->m_startPicoseconds;
	cap->m_timescale = din->m_timescale;

	//Calculate bounds for the *meaningful* output data.
	//Since we're phase shifting, there's gonna be some garbage response at one end of the channel.
	size_t istart = 0;
	size_t iend = npoints;
	if(invert)
		iend -= groupdelay_samples;
	else
		istart += groupdelay_samples;

	//Copy waveform data after rescaling
	float scale = 1.0f / npoints;
	float vmin = FLT_MAX;
	float vmax = -FLT_MAX;
	for(size_t i=istart; i<iend; i++)
	{
		cap->m_offsets.push_back(din->m_offsets[i]);
		cap->m_durations.push_back(din->m_durations[i]);
		float v = ddout[i] * scale;
		vmin = min(v, vmin);
		vmax = max(v, vmax);
		cap->m_samples.push_back(v);
	}

	//Calculate bounds
	m_max = max(m_max, vmax);
	m_min = min(m_min, vmin);
	m_range = (m_max - m_min) * 1.05;
	m_offset = -( (m_max - m_min)/2 + m_min );

	SetData(cap);

	//Clean up
#ifdef _WIN32
	_aligned_free(rdin);
	_aligned_free(rdout);
	_aligned_free(ddout);
#else
	free(rdin);
	free(rdout);
	free(ddout);
#endif
}
