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
#include "CTLEDecoder.h"
#include <ffts.h>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

CTLEDecoder::CTLEDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_ANALYSIS)
{
	//Set up channels
	m_signalNames.push_back("din");
	m_channels.push_back(NULL);

	m_dcGainName = "DC Gain (dB)";
	m_parameters[m_dcGainName] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_FLOAT);
	m_parameters[m_dcGainName].SetFloatVal(0);

	m_zeroFreqName = "Zero Frequency";
	m_parameters[m_zeroFreqName] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_FLOAT);
	m_parameters[m_zeroFreqName].SetFloatVal(1e9);

	m_poleFreq1Name = "Pole Frequency 1";
	m_parameters[m_poleFreq1Name] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_FLOAT);
	m_parameters[m_poleFreq1Name].SetFloatVal(1e9);

	m_poleFreq2Name = "Pole Frequency 2";
	m_parameters[m_poleFreq2Name] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_FLOAT);
	m_parameters[m_poleFreq2Name].SetFloatVal(2e9);

	m_acGainName = "Peak Gain (dB)";
	m_parameters[m_acGainName] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_FLOAT);
	m_parameters[m_acGainName].SetFloatVal(6);

	m_range = 1;
	m_offset = 0;
	m_min = FLT_MAX;
	m_max = -FLT_MAX;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool CTLEDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

double CTLEDecoder::GetVoltageRange()
{
	return m_range;
}

double CTLEDecoder::GetOffset()
{
	return m_offset;
}

string CTLEDecoder::GetProtocolName()
{
	return "CTLE";
}

bool CTLEDecoder::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool CTLEDecoder::NeedsConfig()
{
	return true;
}

void CTLEDecoder::SetDefaultName()
{
	Unit db(Unit::UNIT_DB);
	Unit hz(Unit::UNIT_HZ);

	float dcgain = m_parameters[m_dcGainName].GetFloatVal();
	float zfreq = m_parameters[m_zeroFreqName].GetFloatVal();

	float pole1 = m_parameters[m_poleFreq1Name].GetFloatVal();
	float pole2 = m_parameters[m_poleFreq2Name].GetFloatVal();

	float acgain = m_parameters[m_acGainName].GetFloatVal();

	char hwname[256];
	snprintf(
		hwname,
		sizeof(hwname),
		"CTLE(%s, %s, %s, %s, %s, %s)",
		m_channels[0]->m_displayname.c_str(),
		db.PrettyPrint(dcgain).c_str(),
		hz.PrettyPrint(zfreq).c_str(),
		hz.PrettyPrint(pole1).c_str(),
		hz.PrettyPrint(pole2).c_str(),
		db.PrettyPrint(acgain).c_str()
		);

	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void CTLEDecoder::ClearSweeps()
{
	m_range = 1;
	m_offset = 0;
	m_min = FLT_MAX;
	m_max = -FLT_MAX;
}

void CTLEDecoder::Refresh()
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
	const size_t npoints_raw = din->m_samples.size();
	if(npoints_raw == 0)
	{
		SetData(NULL);
		return;
	}

	//Zero pad to next power of two up
	const size_t npoints = pow(2, ceil(log2(npoints_raw)));

	//Format the input data as raw samples for the FFT
	//TODO: handle non-uniform sample rates
	float* rdin;
	size_t insize = npoints * sizeof(float);

#ifdef _WIN32
	rdin = (float*)_aligned_malloc(insize, 32);
#else
	posix_memalign((void**)&rdin, 32, insize);
#endif

	//Copy the input, then fill any extra space with zeroes
	memcpy(rdin, &din->m_samples[0], npoints_raw*sizeof(float));
	for(size_t i=npoints_raw; i<npoints; i++)
		rdin[i] = 0;

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

	//Pull out our settings
	float dcgain_db = m_parameters[m_dcGainName].GetFloatVal();
	float zfreq = m_parameters[m_zeroFreqName].GetFloatVal();
	float pole1 = m_parameters[m_poleFreq1Name].GetFloatVal();
	float pole2 = m_parameters[m_poleFreq2Name].GetFloatVal();
	float acgain_db = m_parameters[m_acGainName].GetFloatVal();

	//Do the actual equalization
	for(size_t i=0; i<nouts; i++)
	{
		float freq = bin_hz * i;

		float gain;

		//For now, piecewise response. We should smooth this!
		//How can we get a nicer looking transfer function?

		//Below zero, use DC gain
		float db;
		if(freq <= zfreq)
			db = dcgain_db;

		//Then linearly rise to the pole
		//should we interpolate vs F or log(f)?
		else if(freq < pole1)
		{
			float frac = (freq - zfreq) / (pole1 - zfreq);
			db = dcgain_db + (acgain_db - dcgain_db) * frac;
		}

		//Then flat between poles
		else if(freq <= pole2)
			db = acgain_db;

		//Then linear falloff
		else
		{
			db = -30;	//FIXME
			//float scale = (freq - pole2) / pole2;
			//db = acgain_db / scale;
		}

		gain = pow(10, db/20);

		//Amplitude correction
		rdout[i*2 + 0] *= gain;
		rdout[i*2 + 1] *= gain;
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
	/*
	auto& s21 = m_sparams[SPair(2,1)];
	float max_delay = 0;
	for(size_t i=0; i<s21.size()-1 && i<50; i++)
		max_delay = max(max_delay, s21.GetGroupDelay(i));
	int64_t groupdelay_samples = ceil( (max_delay * 1e12) / din->m_timescale);
	*/
	int64_t groupdelay_samples = 0;

	//Set up output and copy timestamps
	auto cap = new AnalogWaveform;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startPicoseconds = din->m_startPicoseconds;
	cap->m_timescale = din->m_timescale;

	//Calculate bounds for the *meaningful* output data.
	//Since we're phase shifting, there's gonna be some garbage response at one end of the channel.
	size_t istart = groupdelay_samples;
	size_t iend = npoints_raw;

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
