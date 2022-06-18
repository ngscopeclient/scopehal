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
#include "TDRStepDeEmbedFilter.h"
#include "FFTFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

TDRStepDeEmbedFilter::TDRStepDeEmbedFilter(const string& color)
	: Filter(color, CAT_ANALYSIS)
{
	m_xAxisUnit = Unit(Unit::UNIT_HZ);
	AddStream(Unit(Unit::UNIT_DB), "data", Stream::STREAM_TYPE_ANALOG);

	//Set up channels
	CreateInput("step");

	m_plan = NULL;
	m_cachedPlanSize = 0;

	m_numAverages = 0;
}

TDRStepDeEmbedFilter::~TDRStepDeEmbedFilter()
{
	if(m_plan)
		ffts_free(m_plan);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool TDRStepDeEmbedFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string TDRStepDeEmbedFilter::GetProtocolName()
{
	return "TDR Step De-Embed";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void TDRStepDeEmbedFilter::ClearSweeps()
{
	m_inputSums.clear();
	m_numAverages = 0;
}

void TDRStepDeEmbedFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	auto din = GetAnalogInputWaveform(0);
	auto cap = SetupEmptyOutputWaveform(din, 0);

	//Calculate the nominal low and high voltages
	Unit volts(Unit::UNIT_VOLTS);
	float vstart = GetBaseVoltage(din);
	float vend = GetTopVoltage(din);
	float vmid = vstart + (vend-vstart)/2;

	//Find the timestamp of the edge
	vector<int64_t> edges;
	FindRisingEdges(din, vmid, edges);
	if(edges.empty())
	{
		LogDebug("No edges found, nothing to do\n");
		return;
	}
	int64_t tedge = edges[0];

	//Figure out FFT size.
	//Pad npoints if too small
	const size_t npoints_raw = din->m_samples.size();
	const size_t npoints_orig = next_pow2(npoints_raw);
	const size_t npoints = npoints_orig;//max(npoints_orig, static_cast<size_t>(128 * 1024));
	const size_t nouts = npoints/2 + 1;

	//New input size? Clear out old state
	if(m_plan && (m_cachedPlanSize != npoints) )
	{
		ffts_free(m_plan);
		m_plan = NULL;
	}

	//Reset inputs as needed
	if(!m_plan)
	{
		m_plan = ffts_init_1d_real(npoints, FFTS_FORWARD);
		m_signalinbuf.resize(npoints);
		m_signaloutbuf.resize(2*nouts);
		m_stepinbuf.resize(npoints);
		m_stepoutbuf.resize(2*nouts);

		//Set up the unit step input signal
		size_t iedge = tedge / din->m_timescale;
		for(size_t i=0; i<npoints; i++)
		{
			if( (i < iedge) || (i > npoints_raw) )
				m_stepinbuf[i] = 0;
			else
				m_stepinbuf[i] = 1;
		}
		FFTFilter::ApplyWindow(&m_stepinbuf[0], npoints_raw, &m_stepinbuf[0], FFTFilter::WINDOW_BLACKMAN_HARRIS);
		ffts_execute(m_plan, &m_stepinbuf[0], &m_stepoutbuf[0]);
	}

	//DEBUG: remove old averages
	//ClearSweeps();

	//Integrate the averages
	//TODO: numerical stability issues if we have too many
	if(m_numAverages == 0)
		m_inputSums.resize(npoints_raw);
	m_numAverages ++;
	for(size_t i=0; i<npoints_raw; i++)
	{
		m_inputSums[i] += din->m_samples[i];
		m_signalinbuf[i] = m_inputSums[i] / m_numAverages;
	}

	//FFT the input signal
	FFTFilter::ApplyWindow(&m_signalinbuf[0], npoints_raw, &m_signalinbuf[0], FFTFilter::WINDOW_BLACKMAN_HARRIS);
	for(size_t i=npoints_raw; i<npoints; i++)
		m_signalinbuf[i] = 0;
	ffts_execute(m_plan, &m_signalinbuf[0], &m_signaloutbuf[0]);

	//Generate the de-embedding filter
	SParameters params;
	params.Clear();
	params.Allocate();
	auto& s11 = params[SPair(1, 1)];
	auto& s12 = params[SPair(1, 2)];
	auto& s21 = params[SPair(2, 1)];
	auto& s22 = params[SPair(2, 2)];

	//Generate the output
	float fs_per_sample = din->m_timescale * (din->m_offsets[1] - din->m_offsets[0]);
	float sample_ghz = 1e6 / fs_per_sample;
	float bin_hz = round((0.5f * sample_ghz * 1e9f) / nouts);
	cap->m_densePacked = true;
	cap->m_timescale = bin_hz;
	cap->Resize(nouts);
	for(size_t i=0; i<nouts; i++)
	{
		//Fetch the FFT outputs
		float real = m_signaloutbuf[i*2 + 0];
		float imag = m_signaloutbuf[i*2 + 1];
		float real_orig = m_stepoutbuf[i*2 + 0];
		float imag_orig = m_stepoutbuf[i*2 + 1];

		/*
			De-embedding equations:
			real = real_orig*cosval - imag_orig*sinval
			imag = real_orig*sinval + imag_orig*cosval

			Knowns: real, imag, real_orig, imag_orig
			Unknowns: sinval, cosval

			real_orig*cosval = real + imag_orig*sinval
			cosval = (real + imag_orig*sinval) / real_orig

			imag_orig*cosval = imag - real_orig*sinval
			cosval = (imag - real_orig*sinval) / imag_orig

			(real + imag_orig*sinval) / real_orig = (imag - real_orig*sinval) / imag_orig

			(real + imag_orig*sinval) * imag_orig = (imag - real_orig*sinval) * real_orig
			real*imag_orig + imag_orig*imag_orig*sinval = imag*real_orig - real_orig*real_orig*sinval

			imag_orig*imag_orig*sinval + real_orig*real_orig*sinval = imag*real_orig - real*imag_orig
			sinval*(imag_orig*imag_orig + real_orig*real_orig) = imag*real_orig - real*imag_orig

			sinval = (imag*real_orig - real*imag_orig) / (imag_orig*imag_orig + real_orig*real_orig)
			cosval = (imag - real_orig*sinval) / imag_orig
		 */
		float sinval = (imag*real_orig - real*imag_orig) / (imag_orig*imag_orig + real_orig*real_orig);
		float cosval = (imag - real_orig*sinval) / imag_orig;

		//Given sin/cosine values, calculate mag and angle
		float mag = sqrt(sinval*sinval + cosval*cosval);
		float angle = asin(sinval / mag);//atan2(cosval, sinval);

		//Unity gain and no phase for the first and last bins
		if( (i == 0) || (i == nouts-1) )
		{
			mag = 1;
			angle = 0;
		}

		float freq = bin_hz * i;

		//Don't touch gain/magnitude beyond scope BW
		//TODO: make this configurable
		if(freq > 16e9)
		{
			mag = 1;
			angle = 0;
		}

		//Save output
		s21.m_points.push_back(SParameterPoint(freq, mag, angle));
		cap->m_samples[i] = 10 * log10(mag);
		cap->m_offsets[i] = i;
		cap->m_durations[i] = 1;

		//Clear other S-parameters
		s11.m_points.push_back(SParameterPoint(freq, 0, 0));
		s12.m_points.push_back(SParameterPoint(freq, 0, 0));
		s22.m_points.push_back(SParameterPoint(freq, 0, 0));
	}

	//Output the resulting data to a Touchstone file
	params.SaveToFile("/tmp/foo.s2p");
}
