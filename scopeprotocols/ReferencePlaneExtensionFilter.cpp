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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Implementation of ReferencePlaneExtensionFilter
 */
#include "scopeprotocols.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ReferencePlaneExtensionFilter::ReferencePlaneExtensionFilter(const string& color)
	: SParameterFilter(color, CAT_RF)
{
	m_parameters[m_portCountName].signal_changed().connect(sigc::mem_fun(*this, &ReferencePlaneExtensionFilter::OnPortCountChanged));
	OnPortCountChanged();
}

ReferencePlaneExtensionFilter::~ReferencePlaneExtensionFilter()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string ReferencePlaneExtensionFilter::GetProtocolName()
{
	return "Reference Plane Extension";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Main filter processing

/**
	@brief Update the set of active parameters
 */
void ReferencePlaneExtensionFilter::OnPortCountChanged()
{
	size_t nports_cur = m_parameters[m_portCountName].GetIntVal();
	size_t nports_old = m_portParamNames.size();

	//Delete old parameters
	vector<string> paramsToDelete;
	for(size_t i=nports_cur; i<nports_old; i++)
		paramsToDelete.push_back(m_portParamNames[i]);
	for(auto p : paramsToDelete)
		m_parameters.erase(p);
	m_portParamNames.resize(nports_cur);

	//Add new ones
	for(size_t i=nports_old; i<nports_cur; i++)
	{
		string name = string("Port ") + to_string(i+1) + " extension";
		m_portParamNames[i] = name;
		m_parameters[name] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));
		m_parameters[name].SetIntVal(0);
	}

	//Notify dialogs etc that we have new parameters
	m_parametersChangedSignal.emit();
}

void ReferencePlaneExtensionFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	//Update timestamp to our *current* timestamp
	double tnow = GetTime();
	int64_t sec = floor(tnow);
	int64_t fs = (tnow - sec) * FS_PER_SECOND;

	size_t nports = m_parameters[m_portCountName].GetIntVal();
	for(size_t to=0; to<nports; to++)
	{
		for(size_t from=0; from<nports; from++)
		{
			//Copy magnitude channel as-is
			size_t imag = (to*nports + from) * 2;
			auto mag_in = GetAnalogInputWaveform(imag);
			auto mag_out = SetupOutputWaveform(mag_in, imag, 0, 0);
			memcpy(&mag_out->m_samples[0], &mag_in->m_samples[0], sizeof(float) * mag_in->m_samples.size());

			//TODO: Copy magnitude data to parameters

			//Copy magnitude gain/offset
			SetVoltageRange(imag, GetInput(imag).GetVoltageRange());
			SetOffset(imag, GetInput(imag).GetOffset());

			//Shift the angle data
			size_t iang = imag + 1;
			auto ang_in = GetAnalogInputWaveform(iang);
			auto ang_out = SetupOutputWaveform(ang_in, iang, 0, 0);
			size_t alen = ang_in->m_samples.size();
			for(size_t i=0; i<alen; i++)
			{
				//Frequency of this point
				int64_t freq = (ang_in->m_timescale * ang_in->m_offsets[i]) + ang_in->m_triggerPhase;
				double period_fs = FS_PER_SECOND / freq;

				int64_t phase_fs =
					m_parameters[m_portParamNames[to]].GetIntVal() +
					m_parameters[m_portParamNames[from]].GetIntVal();

				double phase_frac = fmodf(phase_fs / period_fs, 1);
				double phase_deg = phase_frac * 360;

				double phase_shifted = ang_in->m_samples[i] + phase_deg;
				ang_out->m_samples[i] = phase_shifted;

				mag_out->m_startTimestamp = tnow;
				mag_out->m_startFemtoseconds = fs;
				ang_out->m_startTimestamp = tnow;
				ang_out->m_startFemtoseconds = fs;
			}

			//TODO: Copy angle data to parameters

			//Copy angle gain/offset
			SetVoltageRange(iang, GetInput(iang).GetVoltageRange());
			SetOffset(iang, GetInput(iang).GetOffset());
		}
	}
}
