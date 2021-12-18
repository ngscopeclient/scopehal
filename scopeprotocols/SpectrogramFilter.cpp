/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2021 Andrew D. Zonenberg and contributors                                                         *
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
#include "../scopehal/AlignedAllocator.h"
#include "SpectrogramFilter.h"
#include <immintrin.h>
#include "../scopehal/avx_mathfun.h"
#include "FFTFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SpectrogramWaveform::SpectrogramWaveform(size_t width, size_t height, float fmax, int64_t tstart, int64_t duration)
	: m_width(width)
	, m_height(height)
	, m_fmax(fmax)
	, m_tstart(tstart)
	, m_duration(duration)
{
	size_t npix = width*height;
	m_data = new float[npix];
	for(size_t i=0; i<npix; i++)
		m_data[i] = 0;
}

SpectrogramWaveform::~SpectrogramWaveform()
{
	delete[] m_data;
	m_data = NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SpectrogramFilter::SpectrogramFilter(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_SPECTROGRAM, color, CAT_RF)
	, m_windowName("Window")
	, m_fftLengthName("FFT length")
	, m_rangeMinName("Range Min")
	, m_rangeMaxName("Range Max")
{
	SetYAxisUnits(Unit(Unit::UNIT_HZ), 0);

	//Set up channels
	CreateInput("din");
	m_plan = NULL;

	//Default config
	m_range = 1e9;
	m_offset = -5e8;
	m_cachedFFTLength = 0;

	m_parameters[m_windowName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_windowName].AddEnumValue("Blackman-Harris", FFTFilter::WINDOW_BLACKMAN_HARRIS);
	m_parameters[m_windowName].AddEnumValue("Hamming", FFTFilter::WINDOW_HAMMING);
	m_parameters[m_windowName].AddEnumValue("Hann", FFTFilter::WINDOW_HANN);
	m_parameters[m_windowName].AddEnumValue("Rectangular", FFTFilter::WINDOW_RECTANGULAR);
	m_parameters[m_windowName].SetIntVal(FFTFilter::WINDOW_BLACKMAN_HARRIS);

	m_parameters[m_fftLengthName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_SAMPLEDEPTH));
	m_parameters[m_fftLengthName].AddEnumValue("64", 64);
	m_parameters[m_fftLengthName].AddEnumValue("128", 128);
	m_parameters[m_fftLengthName].AddEnumValue("256", 256);
	m_parameters[m_fftLengthName].AddEnumValue("512", 512);
	m_parameters[m_fftLengthName].AddEnumValue("1024", 1024);
	m_parameters[m_fftLengthName].AddEnumValue("2048", 2048);
	m_parameters[m_fftLengthName].AddEnumValue("4096", 4096);
	m_parameters[m_fftLengthName].AddEnumValue("8192", 8192);
	m_parameters[m_fftLengthName].AddEnumValue("16384", 16384);
	m_parameters[m_fftLengthName].SetIntVal(512);

	m_parameters[m_rangeMaxName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_DBM));
	m_parameters[m_rangeMaxName].SetFloatVal(-10);

	m_parameters[m_rangeMinName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_DBM));
	m_parameters[m_rangeMinName].SetFloatVal(-50);
}

SpectrogramFilter::~SpectrogramFilter()
{
	if(m_plan)
		ffts_free(m_plan);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool SpectrogramFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

double SpectrogramFilter::GetOffset()
{
	return m_offset;
}

double SpectrogramFilter::GetVoltageRange()
{
	return m_range;
}

void SpectrogramFilter::SetVoltageRange(double range)
{
	m_range = range;
}

void SpectrogramFilter::SetOffset(double offset)
{
	m_offset = offset;
}

string SpectrogramFilter::GetProtocolName()
{
	return "Spectrogram";
}

bool SpectrogramFilter::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool SpectrogramFilter::NeedsConfig()
{
	return false;
}

void SpectrogramFilter::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "Spectrogram(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void SpectrogramFilter::ReallocateBuffers(size_t fftlen)
{
	m_cachedFFTLength = fftlen;
	ffts_free(m_plan);
	m_plan = ffts_init_1d_real(fftlen, FFTS_FORWARD);

	m_rdinbuf.resize(fftlen);
	m_rdoutbuf.resize(fftlen + 2);
}

void SpectrogramFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}
	auto din = GetAnalogInputWaveform(0);

	//Figure out how many FFTs to do
	//For now, consecutive blocks and not a sliding window
	size_t inlen = din->m_samples.size();
	size_t fftlen = m_parameters[m_fftLengthName].GetIntVal();
	if(fftlen != m_cachedFFTLength)
		ReallocateBuffers(fftlen);
	size_t nblocks = inlen / fftlen;

	//Figure out range of the FFTs
	double fs_per_sample = din->m_timescale * (din->m_offsets[1] - din->m_offsets[0]);
	float scale = 2.0 / fftlen;
	double sample_ghz = 1e6 / fs_per_sample;
	double bin_hz = round((0.5f * sample_ghz * 1e9f) / fftlen);
	double fmax = bin_hz * fftlen;

	Unit hz(Unit::UNIT_HZ);
	LogTrace("SpectrogramFilter: %zu input points, %zu %zu-point FFTs\n", inlen, nblocks, fftlen);
	LogIndenter li;
	LogTrace("FFT range is DC to %s\n", hz.PrettyPrint(fmax).c_str());
	LogTrace("%s per bin\n", hz.PrettyPrint(bin_hz).c_str());

	//Create the output
	size_t nouts = fftlen/2 + 1;
	auto cap = new SpectrogramWaveform(
		nblocks,
		nouts,
		fmax,
		din->m_offsets[0] * din->m_timescale,
		fs_per_sample * nblocks * fftlen
		);
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	cap->m_triggerPhase = 0;
	cap->m_timescale = bin_hz;
	cap->m_densePacked = true;
	SetData(cap, 0);

	//Run the FFTs
	auto window = static_cast<FFTFilter::WindowFunction>(m_parameters[m_windowName].GetIntVal());
	auto data = cap->GetData();
	const float impedance = 50;
	float minscale = m_parameters[m_rangeMinName].GetFloatVal();
	float fullscale = m_parameters[m_rangeMaxName].GetFloatVal();
	float range = fullscale - minscale;
	for(size_t block=0; block<nblocks; block++)
	{
		//Grab the input and apply the window function
		FFTFilter::ApplyWindow((float*)&din->m_samples[block*fftlen], fftlen, &m_rdinbuf[0], window);

		//Do the actual FFT
		ffts_execute(m_plan, &m_rdinbuf[0], &m_rdoutbuf[0]);

		//TODO: figure out if there's any way to vectorize this
		for(size_t i=0; i<nouts; i++)
		{
			float real = m_rdoutbuf[i*2 + 0];
			float imag = m_rdoutbuf[i*2 + 1];
			float voltage = sqrtf(real*real + imag*imag) * scale;
			float dbm = (10 * log10(voltage*voltage / impedance) + 30);
			if(dbm < minscale)
				data[i*nblocks + block] = 0;
			else
				data[i*nblocks + block] = (dbm - minscale) / range;
		}
	}
}
