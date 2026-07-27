/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
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
#include <complex>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

CTLEFilter::CTLEFilter(const string& color)
	: Filter(color, CAT_ANALYSIS)
	, m_dcGain(m_parameters["DC Gain"])
	, m_zeroFreq(m_parameters["Zero Frequency"])
	, m_poleFreq1(m_parameters["Pole Frequency 1"])
	, m_poleFreq2(m_parameters["Pole Frequency 2"])
	, m_maxFreq(m_parameters["End Frequency"])
	, m_cachedDcGain(1)
	, m_cachedZeroFreq(1)
	, m_cachedPole1Freq(1)
	, m_cachedPole2Freq(1)
	, m_cachedMaxFreq(0)
{
	m_dcGain = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_DB));
	m_dcGain.SetFloatVal(0);

	m_zeroFreq = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ));
	m_zeroFreq.SetFloatVal(1e7);

	m_poleFreq1 = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ));
	m_poleFreq1.SetFloatVal(1e9);

	m_poleFreq2 = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ));
	m_poleFreq2.SetFloatVal(2e9);

	m_maxFreq = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_HZ));
	m_maxFreq.SetFloatVal(5e10);

	//No inputs
	AddStream(Unit(Unit::UNIT_DB), "mag", Stream::STREAM_TYPE_ANALOG);
	AddStream(Unit(Unit::UNIT_DEGREES), "angle", Stream::STREAM_TYPE_ANALOG);
	SetXAxisUnits(Unit::UNIT_HZ);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

//This is intentionally not virtual since it's a static method used by enumeration
//cppcheck-suppress duplInheritedMember
string CTLEFilter::GetProtocolName()
{
	return "CTLE";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void CTLEFilter::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("CTLEFilter::Refresh");
	#endif

	//Pull out our settings
	float dcgain_db = m_dcGain.GetFloatVal();
	float zfreq = m_zeroFreq.GetFloatVal();
	float pole1 = m_poleFreq1.GetFloatVal();
	float pole2 = m_poleFreq2.GetFloatVal();
	int64_t maxFreq = m_maxFreq.GetIntVal();

	if(
		(dcgain_db != m_cachedDcGain) ||
		(zfreq != m_cachedZeroFreq) ||
		(pole1 != m_cachedPole1Freq) ||
		(pole2 != m_cachedPole2Freq) ||
		(maxFreq != m_cachedMaxFreq) )
	{
		m_cachedDcGain = dcgain_db;
		m_cachedZeroFreq = zfreq;
		m_cachedPole1Freq = pole1;
		m_cachedPole2Freq = pole2;
		m_cachedMaxFreq = maxFreq;

		//for now, 10 MHz bin spacing
		double bin_hz = 10e6;
		size_t nbins = maxFreq / bin_hz;

		auto mag = SetupEmptyUniformAnalogOutputWaveform(nullptr, 0);
		auto ang = SetupEmptyUniformAnalogOutputWaveform(nullptr, 1);
		mag->PrepareForCpuAccess();
		ang->PrepareForCpuAccess();

		//Update timestamps
		auto stime = GetTime();
		auto sec = floor(stime);
		auto fs = (stime - sec) * FS_PER_SECOND;
		mag->m_startTimestamp = sec;
		ang->m_startTimestamp = sec;
		mag->m_startFemtoseconds = fs;
		ang->m_startFemtoseconds = fs;

		//Update sample intervals
		mag->m_timescale = bin_hz;
		mag->m_triggerPhase = 0;
		ang->m_timescale = bin_hz;
		ang->m_triggerPhase = 0;

		typedef complex<float> fcpx;
		fcpx p0(0, -FreqToPhase(m_cachedPole1Freq));
		fcpx p1(0, -FreqToPhase(m_cachedPole2Freq));
		fcpx zero(0, -FreqToPhase(m_cachedZeroFreq));

		//Calculate the prescaler to null out the filter gain
		float prescale = 1.0f / abs(zero / (p0*p1) );

		//Multiply by our gain (in dB, so we have to convert to V/V)
		prescale *= pow(10, m_cachedDcGain/20);

		mag->Resize(nbins);
		ang->Resize(nbins);

		for(size_t i=0; i<nbins; i++)
		{
			fcpx s(0, FreqToPhase(bin_hz * i));
			fcpx h = prescale * (s - zero) / ( (s - p0) * (s - p1) );

			//Compute log magnitude
			mag->m_samples[i] = 20 * log10(abs(h));

			//Phase correction seems unnecessary because this transfer function should be constant rotation?
			//We get weird results when we do this, too.
			ang->m_samples[i] = /*arg(h) */ 0;
		}

		mag->MarkModifiedFromCpu();
		ang->MarkModifiedFromCpu();
	}
}
