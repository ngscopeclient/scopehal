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
	: DeEmbedDecoder(color)
{
	//delete the de-embed params
	m_parameters.clear();

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

	m_cachedDcGain = 1;
	m_cachedZeroFreq = 1;
	m_cachedPole1Freq = 1;
	m_cachedPole2Freq = 1;
	m_cachedAcGain = 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

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

bool CTLEDecoder::LoadSparameters()
{
	return true;
}

int64_t CTLEDecoder::GetGroupDelay()
{
	//no phase shift
	return 0;
}

void CTLEDecoder::InterpolateSparameters(float bin_hz, bool /*invert*/, size_t nouts)
{
	m_cachedBinSize = bin_hz;

	for(size_t i=0; i<nouts; i++)
	{
		float freq = bin_hz * i;

		//For now, piecewise response. We should smooth this!
		//How can we get a nicer looking transfer function?

		//Below zero, use DC gain
		float db;
		if(freq <= m_cachedZeroFreq)
			db = m_cachedDcGain;

		//Then linearly rise to the pole
		//should we interpolate vs F or log(f)?
		else if(freq < m_cachedPole1Freq)
		{
			float frac = (freq - m_cachedZeroFreq) / (m_cachedPole1Freq - m_cachedZeroFreq);
			db = m_cachedDcGain + (m_cachedAcGain - m_cachedDcGain) * frac;
		}

		//Then flat between poles
		else if(freq <= m_cachedPole2Freq)
			db = m_cachedAcGain;

		//Then linear falloff
		else
		{
			db = -30;	//FIXME
			//float scale = (freq - pole2) / pole2;
			//db = acgain_db / scale;
		}

		//Gain
		m_resampledSparamAmplitudes.push_back(pow(10, db/20));

		//Zero phase for now
		m_resampledSparamSines.push_back(0);
		m_resampledSparamCosines.push_back(1);
	}
}

void CTLEDecoder::Refresh()
{
	//Pull out our settings
	float dcgain_db = m_parameters[m_dcGainName].GetFloatVal();
	float zfreq = m_parameters[m_zeroFreqName].GetFloatVal();
	float pole1 = m_parameters[m_poleFreq1Name].GetFloatVal();
	float pole2 = m_parameters[m_poleFreq2Name].GetFloatVal();
	float acgain_db = m_parameters[m_acGainName].GetFloatVal();

	 if(
		(dcgain_db != m_cachedDcGain) ||
		(zfreq != m_cachedZeroFreq) ||
		(pole1 != m_cachedPole1Freq) ||
		(pole2 != m_cachedPole2Freq) ||
		(acgain_db != m_cachedAcGain) )
	{
		//force re-interpolation of S-parameters
		m_cachedBinSize = 0;

		m_cachedDcGain = dcgain_db;
		m_cachedZeroFreq = zfreq;
		m_cachedPole1Freq = pole1;
		m_cachedPole2Freq = pole2;
		m_cachedAcGain = acgain_db;
	}

	//Do the actual refresh operation
	DoRefresh(false);
}

