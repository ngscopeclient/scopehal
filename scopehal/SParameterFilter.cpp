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

#include "scopehal.h"

using namespace std;

SParameterFilter::SParameterFilter(const string& color, Category cat)
	: SParameterSourceFilter(color, cat)
	, m_portCountName("Port Count")
{
	m_parameters[m_portCountName] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_portCountName].SetIntVal(2);
	m_parameters[m_portCountName].signal_changed().connect(sigc::mem_fun(*this, &SParameterFilter::RefreshPorts));

	RefreshPorts();
}

SParameterFilter::~SParameterFilter()
{
}


bool SParameterFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	//All inputs are required
	if(stream.m_channel == NULL)
		return false;

	//Must be a valid port number (assume we take a single set of s-params as input)
	size_t nports = m_parameters[m_portCountName].GetIntVal();
	if(i >= (2*nports*nports) )
		return false;

	//X axis must be Hz
	if(stream.GetXAxisUnits() != Unit(Unit::UNIT_HZ))
		return false;

	//Angle: Y axis unit must be degrees
	if(i & 1)
	{
		if(stream.GetYAxisUnits() != Unit(Unit::UNIT_DEGREES))
			return false;
	}

	//Magnitude: Y axis unit must be dB
	else
	{
		if(stream.GetYAxisUnits() != Unit(Unit::UNIT_DB))
			return false;
	}

	return true;
}

void SParameterFilter::RefreshPorts()
{
	m_params.Allocate(m_parameters[m_portCountName].GetIntVal());
	SetupStreams();

	//Create new inputs
	size_t nports = m_parameters[m_portCountName].GetIntVal();
	for(size_t to = 0; to < nports; to++)
	{
		for(size_t from = 0; from < nports; from ++)
		{
			//If we already have this input, do nothing
			if( (to*nports + from)*2 < m_inputs.size())
				continue;

			auto pname = string("S") + to_string(to+1) + to_string(from+1);
			CreateInput(pname + "_mag");
			CreateInput(pname + "_ang");
		}
	}

	//Delete extra inputs
	size_t nin = nports*nports*2;
	for(size_t i=nin; i<m_inputs.size(); i++)
		SetInput(i, NULL, true);
	m_inputs.resize(nin);
	m_signalNames.resize(nin);

	m_inputsChangedSignal.emit();
}
