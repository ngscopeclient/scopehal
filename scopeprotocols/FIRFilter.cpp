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

#include "scopeprotocols.h"
#include "FIRFilter.h"
#include <immintrin.h>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

FIRFilter::FIRFilter(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_MATH)
	, m_filterTypeName("Filter Type")
	, m_filterLengthName("Length")
	, m_stopbandAttenName("Stopband Attenuation")
	, m_freqLowName("Frequency Low")
	, m_freqHighName("Frequency High")
{
	CreateInput("in");

	m_range = 1;
	m_offset = 0;
	m_min = FLT_MAX;
	m_max = -FLT_MAX;

	m_parameters[m_filterTypeName] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_filterTypeName].AddEnumValue("Low pass", FILTER_TYPE_LOWPASS);
	m_parameters[m_filterTypeName].AddEnumValue("High pass", FILTER_TYPE_HIGHPASS);
	m_parameters[m_filterTypeName].AddEnumValue("Band pass", FILTER_TYPE_BANDPASS);
	m_parameters[m_filterTypeName].AddEnumValue("Notch", FILTER_TYPE_NOTCH);
	m_parameters[m_filterTypeName].SetIntVal(FILTER_TYPE_LOWPASS);

	m_parameters[m_filterLengthName] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLEDEPTH));
	m_parameters[m_filterLengthName].SetIntVal(19);

	m_parameters[m_stopbandAttenName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_DB));
	m_parameters[m_stopbandAttenName].SetFloatVal(60);

	m_parameters[m_freqLowName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ));
	m_parameters[m_freqLowName].SetFloatVal(0);

	m_parameters[m_freqHighName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ));
	m_parameters[m_freqHighName].SetFloatVal(100e6);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool FIRFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

void FIRFilter::ClearSweeps()
{
	m_range = 1;
	m_offset = 0;
	m_min = FLT_MAX;
	m_max = -FLT_MAX;
}

void FIRFilter::SetDefaultName()
{
	char hwname[256];
	auto type = static_cast<FilterType>(m_parameters[m_filterTypeName].GetIntVal());
	switch(type)
	{
		case FILTER_TYPE_LOWPASS:
			snprintf(hwname, sizeof(hwname), "LPF(%s, %s)",
				GetInputDisplayName(0).c_str(),
				m_parameters[m_freqHighName].ToString().c_str());
			break;

		case FILTER_TYPE_HIGHPASS:
			snprintf(hwname, sizeof(hwname), "HPF(%s, %s)",
				GetInputDisplayName(0).c_str(),
				m_parameters[m_freqLowName].ToString().c_str());
			break;

		case FILTER_TYPE_BANDPASS:
			snprintf(hwname, sizeof(hwname), "BPF(%s, %s, %s)",
				GetInputDisplayName(0).c_str(),
				m_parameters[m_freqLowName].ToString().c_str(),
				m_parameters[m_freqHighName].ToString().c_str());
			break;

		case FILTER_TYPE_NOTCH:
			snprintf(hwname, sizeof(hwname), "Notch(%s, %s, %s)",
				GetInputDisplayName(0).c_str(),
				m_parameters[m_freqLowName].ToString().c_str(),
				m_parameters[m_freqHighName].ToString().c_str());
			break;

	}
	m_hwname = hwname;
	m_displayname = m_hwname;
}

string FIRFilter::GetProtocolName()
{
	return "FIR Filter";
}

bool FIRFilter::IsOverlay()
{
	//we create a new analog channel
	return false;
}

bool FIRFilter::NeedsConfig()
{
	return true;
}

double FIRFilter::GetVoltageRange()
{
	return m_range;
}

double FIRFilter::GetOffset()
{
	return m_offset;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void FIRFilter::Refresh()
{
	//Sanity check
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	//Get input data
	auto din = GetAnalogInputWaveform(0);

	//Assume the input is dense packed, get the sample frequency
	int64_t fs_per_sample = din->m_timescale;
	float sample_hz = FS_PER_SECOND / fs_per_sample;

	//Calculate limits for our filter
	float nyquist = sample_hz / 2;
	float flo = m_parameters[m_freqLowName].GetFloatVal();
	float fhi = m_parameters[m_freqHighName].GetFloatVal();
	auto type = static_cast<FilterType>(m_parameters[m_filterTypeName].GetIntVal());
	if(type == FILTER_TYPE_LOWPASS)
		flo = 0;
	else if(type == FILTER_TYPE_HIGHPASS)
		fhi = nyquist;
	else
	{
		//Swap high/low if they get swapped
		if(fhi < flo)
		{
			float ftmp = flo;
			flo = fhi;
			fhi = ftmp;
		}
	}
	flo = max(flo, 0.0f);
	fhi = min(fhi, nyquist);

	//Create the filter coefficients (TODO: cache this)
	size_t filterlen = m_parameters[m_filterLengthName].GetIntVal() | 1;	//force length to be odd
	vector<float> coeffs;
	coeffs.resize(filterlen);
	CalculateFilterCoefficients(
		coeffs,
		flo / nyquist,
		fhi / nyquist,
		m_parameters[m_stopbandAttenName].GetFloatVal(),
		type
		);

	//Set up output
	m_xAxisUnit = m_inputs[0].m_channel->GetXAxisUnits();
	m_yAxisUnit = m_inputs[0].m_channel->GetYAxisUnits();
	size_t radius = (filterlen - 1) / 2;
	auto cap = SetupOutputWaveform(din, 0, 0, filterlen);

	//Run the actual filter
	float vmin;
	float vmax;
	DoFilterKernel(coeffs, din, cap, vmin, vmax);

	//Correct for phase shift
	cap->m_triggerPhase = (radius * fs_per_sample) + din->m_triggerPhase;

	//Calculate bounds
	m_max = max(m_max, vmax);
	m_min = min(m_min, vmin);
	m_range = (m_max - m_min) * 1.05;
	m_offset = -( (m_max - m_min)/2 + m_min );
}

void FIRFilter::DoFilterKernel(
	vector<float>& coefficients,
	AnalogWaveform* din,
	AnalogWaveform* cap,
	float& vmin,
	float& vmax)
{
	/*if(g_hasAvx2)
		DoFilterKernelAVX2(coefficients, din, cap, vmin, vmax);
	else*/
		DoFilterKernelGeneric(coefficients, din, cap, vmin, vmax);
}

/**
	@brief Performs a FIR filter (does not assume symmetric)
 */
void FIRFilter::DoFilterKernelGeneric(
	vector<float>& coefficients,
	AnalogWaveform* din,
	AnalogWaveform* cap,
	float& vmin,
	float& vmax)
{
	//Setup
	vmin = FLT_MAX;
	vmax = -FLT_MAX;
	size_t len = din->m_samples.size();
	size_t filterlen = coefficients.size();
	size_t end = len - filterlen;

	//Do the filter
	for(size_t i=0; i<end; i++)
	{
		float v = 0;
		for(size_t j=0; j<filterlen; j++)
			v += din->m_samples[i + j] * coefficients[j];

		vmin = min(vmin, v);
		vmax = max(vmax, v);

		cap->m_samples[i]	= v;
	}
}

/**
	@brief Calculates FIR coefficients

	Based on public domain code at https://www.arc.id.au/FilterDesign.html

	Cutoff frequencies are specified in fractions of the Nyquist limit (Fsample/2).

	@param coefficients		Output buffer
	@param fa				Left side passband (0 for LPF)
	@param fb				Right side passband (1 for HPF)
	@param stopbandAtten	Stop-band attenuation, in dB
	@param type				Type of filter
 */
void FIRFilter::CalculateFilterCoefficients(
	vector<float>& coefficients,
	float fa,
	float fb,
	float stopbandAtten,
	FilterType type)
{
	//Calculate the impulse response of the filter
	size_t len = coefficients.size();
	size_t np = (len - 1) / 2;
	vector<float> impulse;
	impulse.push_back(fb-fa);
	for(size_t j=1; j<=np; j++)
		impulse.push_back( (sin(j*M_PI*fb) - sin(j*M_PI*fa)) /(j*M_PI) );

	//Calculate window scaling factor for stopband attenuation
	float alpha = 0;
	if(stopbandAtten < 21)
		alpha = 0;
	else if(stopbandAtten > 50)
		alpha = 0.1102 * (stopbandAtten - 8.7);
	else
		alpha = 0.5842 * pow(stopbandAtten-21, 0.4) + 0.07886*(stopbandAtten-21);

	//Final windowing (Kaiser-Bessel)
	float ia = Bessel(alpha);
	if(type == FILTER_TYPE_NOTCH)
	{
		for(size_t j=0; j<=np; j++)
			coefficients[np+j] = -impulse[j] * Bessel(alpha * sqrt(1 - ((j*j*1.0)/(np*np)))) / ia;
		coefficients[np] += 1;
	}
	else
	{
		for(size_t j=0; j<=np; j++)
			coefficients[np+j] = impulse[j] * Bessel(alpha * sqrt(1 - ((j*j*1.0)/(np*np)))) / ia;
	}
	for(size_t j=0; j<=np; j++)
		coefficients[j] = coefficients[len-1-j];
}

/**
	@brief 0th order Bessel function
 */
float FIRFilter::Bessel(float x)
{
	float d = 0;
	float ds = 1;
	float s = 1;
	while(ds > s*1e-6)
	{
		d += 2;
		ds *= (x*x)/(d*d);
		s += ds;
	}
	return s;
}
