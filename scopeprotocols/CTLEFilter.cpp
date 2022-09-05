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
#include "CTLEFilter.h"
#include <ffts.h>
#include <complex>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

CTLEFilter::CTLEFilter(const string& color)
	: DeEmbedFilter(color)
{
	//delete the de-embed params
	m_parameters.clear();

	m_dcGainName = "DC Gain";
	m_parameters[m_dcGainName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_DB));
	m_parameters[m_dcGainName].SetFloatVal(0);

	m_zeroFreqName = "Zero Frequency";
	m_parameters[m_zeroFreqName] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ));
	m_parameters[m_zeroFreqName].SetFloatVal(1e7);

	m_poleFreq1Name = "Pole Frequency 1";
	m_parameters[m_poleFreq1Name] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ));
	m_parameters[m_poleFreq1Name].SetFloatVal(1e9);

	m_poleFreq2Name = "Pole Frequency 2";
	m_parameters[m_poleFreq2Name] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ));
	m_parameters[m_poleFreq2Name].SetFloatVal(2e9);

	m_cachedDcGain = 1;
	m_cachedZeroFreq = 1;
	m_cachedPole1Freq = 1;
	m_cachedPole2Freq = 1;

	//delete s-param inputs
	m_signalNames.resize(1);
	m_inputs.resize(1);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string CTLEFilter::GetProtocolName()
{
	return "CTLE";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

int64_t CTLEFilter::GetGroupDelay()
{
	//no phase shift
	return 0;
}

void CTLEFilter::InterpolateSparameters(float bin_hz, bool /*invert*/, size_t nouts)
{
	m_cachedBinSize = bin_hz;

	typedef complex<float> fcpx;

	fcpx p0(0, -FreqToPhase(m_cachedPole1Freq));
	fcpx p1(0, -FreqToPhase(m_cachedPole2Freq));
	fcpx zero(0, -FreqToPhase(m_cachedZeroFreq));

	//Calculate the prescaler to null out the filter gain
	float prescale = 1.0f / abs(zero / (p0*p1) );

	//Multiply by our gain (in dB, so we have to convert to V/V)
	prescale *= pow(10, m_cachedDcGain/20);

	for(size_t i=0; i<nouts; i++)
	{
		fcpx s(0, FreqToPhase(bin_hz * i));
		fcpx h = prescale * (s - zero) / ( (s-p0) * (s-p1) );

		//Phase correction seems unnecessary because this transfer function should be constant rotation?
		//We get weird results when we do this, too.
		float phase = 0;//arg(h);
		m_resampledSparamSines.push_back(sin(phase) * abs(h));
		m_resampledSparamCosines.push_back(cos(phase) * abs(h));
	}
}

void CTLEFilter::Refresh(vk::raii::CommandBuffer& cmdBuf, vk::raii::Queue& queue)
{
	//Pull out our settings
	float dcgain_db = m_parameters[m_dcGainName].GetFloatVal();
	float zfreq = m_parameters[m_zeroFreqName].GetFloatVal();
	float pole1 = m_parameters[m_poleFreq1Name].GetFloatVal();
	float pole2 = m_parameters[m_poleFreq2Name].GetFloatVal();

	 if(
		(dcgain_db != m_cachedDcGain) ||
		(zfreq != m_cachedZeroFreq) ||
		(pole1 != m_cachedPole1Freq) ||
		(pole2 != m_cachedPole2Freq) )
	{
		//force re-interpolation of S-parameters
		m_cachedBinSize = 0;

		m_cachedDcGain = dcgain_db;
		m_cachedZeroFreq = zfreq;
		m_cachedPole1Freq = pole1;
		m_cachedPole2Freq = pole2;
	}

	//Do the actual refresh operation
	DoRefresh(false, cmdBuf, queue);
}

