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
#include <complex>
#include "OFDMDemodulator.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

OFDMDemodulator::OFDMDemodulator(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, CAT_RF)
{
	//Set up channels
	CreateInput("I");
	CreateInput("Q");

	m_range = 1;
	m_offset = 0;

	m_min = FLT_MAX;
	m_max = -FLT_MAX;

	//TODO: create outputs

	m_symbolTimeName = "Symbol Time";
	m_parameters[m_symbolTimeName] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));
	m_parameters[m_symbolTimeName].SetIntVal(3.2e9);

	m_guardIntervalName = "Guard Interval";
	m_parameters[m_guardIntervalName] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));
	m_parameters[m_guardIntervalName].SetIntVal(400e6);

	m_fftSizeName = "FFT Size";
	m_parameters[m_fftSizeName] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_fftSizeName].SetIntVal(64);

	m_cachedFftSize = 0;
	m_fftPlan = NULL;
	m_fftInputBuf = NULL;
	m_fftOutputBuf = NULL;

	//Constant 16 point FFT
	m_fftPlan16 = ffts_init_1d(16, FFTS_FORWARD);
}

OFDMDemodulator::~OFDMDemodulator()
{
	ffts_free(m_fftPlan);
	ffts_free(m_fftPlan16);

	m_allocator.deallocate(m_fftInputBuf);
	m_allocator.deallocate(m_fftOutputBuf);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool OFDMDemodulator::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 2) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

float OFDMDemodulator::GetVoltageRange(size_t /*stream*/)
{
	return m_range;
}

float OFDMDemodulator::GetOffset(size_t /*stream*/)
{
	return -m_offset;
}

string OFDMDemodulator::GetProtocolName()
{
	return "OFDM Demodulator";
}

bool OFDMDemodulator::NeedsConfig()
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void OFDMDemodulator::ClearSweeps()
{
	m_range = 1;
	m_offset = 0;
	m_min = FLT_MAX;
	m_max = -FLT_MAX;
}

void OFDMDemodulator::Refresh()
{
	//Not implemented, do nothing
	SetData(NULL, 0);
	return;
	/*
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the sample data
	auto din_i = GetAnalogInputWaveform(0);
	auto din_q = GetAnalogInputWaveform(1);

	//Copy the units
	m_yAxisUnit = m_inputs[0].m_channel->GetYAxisUnits();

	//Convert rates to number of samples
	int symbol_time_fs = m_parameters[m_symbolTimeName].GetIntVal();
	size_t symbol_time_samples = symbol_time_ps / din_i->m_timescale;
	int guard_interval_fs = m_parameters[m_guardIntervalName].GetIntVal();
	size_t guard_interval_samples = guard_interval_fs / din_i->m_timescale;

	//We need meaningful data, bail if it's too short
	auto len = min(din_i->m_samples.size(), din_q->m_samples.size());
	if(len < symbol_time_samples)
	{
		SetData(NULL, 0);
		return;
	}

	//Worry about the output later

	//First pass: Find symbol sync (Schmidl-Cox)
	//TODO: break up multi packet waveforms
	//TODO: we can get a better idea of sync by looking at the *slope* of the metric, it should be flat topped.
	//Find the left and right edges then look at the midpoint maybe?
	size_t period_samples = symbol_time_samples + guard_interval_samples;
	size_t end = len - 2*period_samples;
	float max_metric = -FLT_MAX;
	size_t imax = 0;
	for(size_t i=0; i < end; i ++)
	{
		complex<float> total = 0;
		for(size_t j=0; j<guard_interval_samples; j++)
		{
			size_t first = i + j;
			size_t second = first + period_samples;

			complex<float> a(din_i->m_samples[first], din_q->m_samples[first]);
			complex<float> b(din_i->m_samples[second], din_q->m_samples[second]);

			total += a*b;
		}

		float metric = fabs(total);

		if(metric > max_metric)
		{
			max_metric = metric;
			imax = i;
		}
	}

	//Create FFT config and buffers
	int fftsize = m_parameters[m_fftSizeName].GetIntVal();
	if(fftsize != m_cachedFftSize)
	{
		m_cachedFftSize = fftsize;

		if(m_fftPlan)
			ffts_free(m_fftPlan);
		m_fftPlan = ffts_init_1d(fftsize, FFTS_FORWARD);

		if(m_fftInputBuf)
			m_allocator.deallocate(m_fftInputBuf);
		m_fftInputBuf = m_allocator.allocate(fftsize*2);

		if(m_fftOutputBuf)
			m_allocator.deallocate(m_fftOutputBuf);
		m_fftOutputBuf = m_allocator.allocate(fftsize*2);
	}

	//everything after here is probably 802.11n specific for now?
	if(fftsize != 64)
	{
		SetData(NULL, 0);
		return;
	}

	//DEBUG: rely on scope trigger for initial sync
	imax = 0;

	//Print header
	static bool first = true;
	if(first)
	{
		first = false;
		LogDebug("symbol,");
		for(int i=0; i<fftsize; i++)
			LogDebug("i%d,q%d,", i, i);
		LogDebug("\n");
	}

	//Read the short preamble. Ten blocks of 800ns symbols with no guard interval.
	//We're sampling at 50ns per sample so this is 16 samples.
	//Maybe we should do a 16 point FFT on each?
	float preambleAmplitudes[12] = {0};
	float initialPreamblePhases[12] = {0};
	int preambleToneBins[12] = {1, 2, 3, 4, 5, 6, 10, 11, 12, 13, 14, 15};
	size_t unstableCount	= 0;	//number of preamble samples we consider unstable
									//(ESP32s etc transmit early before full PLL lock!)
	float phaseDriftPerCarrier[12] = {0};
	for(size_t block=0; block<10; block ++)
	{
		//Copy I/Q sample data
		size_t ibase = imax + 16*block;
		for(int j=0; j<16; j++)
		{
			m_fftInputBuf[j*2] = din_i->m_samples[ibase + j];
			m_fftInputBuf[j*2 + 1] = din_q->m_samples[ibase + j];
		}

		//Do the FFT
		ffts_execute(m_fftPlan16, m_fftInputBuf, m_fftOutputBuf);

		//Process each symbol
		for(size_t i=0; i<12; i++)
		{
			//Average the amplitudes
			int bin = preambleToneBins[i];
			complex<float> c(m_fftOutputBuf[bin*2], m_fftOutputBuf[bin*2 + 1]);
			preambleAmplitudes[i] += abs(c);

			//Calculate inital phase
			if(block == unstableCount)
				initialPreamblePhases[i] = arg(c);
			else if(block > unstableCount)
			{
				complex<float> d = polar(1.0f, arg(c) - initialPreamblePhases[i]);
				if(block == 9)
				{
					phaseDriftPerCarrier[i] = arg(d) / (9 - unstableCount);
					preambleAmplitudes[i] /= 10;

					//New phase starts here
					initialPreamblePhases[i] = arg(c);
				}
			}
		}
	}

	LogDebug("0,");
	for(size_t i=0; i<12; i++)
		LogDebug("%12f,", phaseDriftPerCarrier[i]);
	LogDebug("\n");

	//Skip the short preamble
	imax += 160;
	float startingPhase = initialPreamblePhases[0];

	//Calculate average drift across all preamble symbols
	float driftPer800ns = 0;
	float ampScale = 0;
	for(size_t i=0; i<12; i++)
	{
		driftPer800ns += phaseDriftPerCarrier[i];
		ampScale += preambleAmplitudes[i];
	}
	driftPer800ns /= 12;
	ampScale /= 12;

	float driftPerSymbol = 5 * driftPer800ns;

	//Skip 1.6us (32 sample) guard interval
	imax += 32;
	startingPhase += 2*driftPer800ns;
	*/

	//We should now have two OFDM BPSK training symbols with no guard interval
	//Carrier in columns 8, 12, 16, 20, 24, 40, 44, 48, 52, 56, 60??
	/*
	for(size_t i=0; i<2; i++)
	{
		//Copy I/Q sample data
		size_t ibase = imax + symbol_time_samples*i;
		for(int j=0; j<fftsize; j++)
		{
			m_fftInputBuf[j*2] = din_i->m_samples[ibase + j];
			m_fftInputBuf[j*2 + 1] = din_q->m_samples[ibase + j];
		}

		//Run the FFT
		ffts_execute(m_fftPlan, m_fftInputBuf, m_fftOutputBuf);

		//Grab each output
		LogDebug("%zu,", i);
		for(int j=0; j<fftsize; j++)
		{
			//Rotate by the calculated drift
			complex<float> c(m_fftOutputBuf[j*2], m_fftOutputBuf[j*2 + 1]);

			float phase = startingPhase + (4*driftPer800ns*i);
			c *= polar(1.0f, -phase);
			c /= ampScale;

			LogDebug("%12f,%12f,", c.real(), c.imag());
		}
		LogDebug("\n");
	}*/

	//Skip these training symbols
	//imax += 128;
	//startingPhase += 8*driftPer800ns;

	//Actual symbol data
	/*
	size_t iblock = 0;
	for(size_t i=imax; i<end; i += period_samples)
	{
		//Copy I/Q sample data
		//Initial sync point is the first symbol's cyclic prefix.
		//Skip the guard interval when copying the actual samples.
		size_t ibase = i + guard_interval_samples;
		for(int j=0; j<fftsize; j++)
		{
			m_fftInputBuf[j*2] = din_i->m_samples[ibase + j];
			m_fftInputBuf[j*2 + 1] = din_q->m_samples[ibase + j];
		}

		//Run the FFT
		ffts_execute(m_fftPlan, m_fftInputBuf, m_fftOutputBuf);

		LogDebug("%5zu,", iblock);

		//Phase correction
		for(size_t j=0; j<64; j++)
		{
			complex<float> c(m_fftOutputBuf[j*2], m_fftOutputBuf[j*2 + 1]);

			//Track pilot tone in bin 7
			//complex<float> pilot(m_fftOutputBuf[7*2], m_fftOutputBuf[7*2 + 1]);
			//float phase = arg(pilot);

			float phase = startingPhase + driftPerSymbol*iblock;
			c *= polar(1.0f, -phase);
			c /= ampScale;

			LogDebug("%12f,%12f,", c.real(), c.imag());
		}
		LogDebug("\n");

		iblock ++;
		break;
	}*/

	//TODO
	SetData(NULL, 0);
	return;
}
