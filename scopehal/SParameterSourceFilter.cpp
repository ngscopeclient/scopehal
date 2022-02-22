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
#include "SParameterSourceFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SParameterSourceFilter::SParameterSourceFilter(const string& color, Category cat)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_ANALOG, color, cat)
{
	SetupStreams();
	SetXAxisUnits(Unit(Unit::UNIT_HZ));
}

SParameterSourceFilter::~SParameterSourceFilter()
{
}

bool SParameterSourceFilter::NeedsConfig()
{
	return true;
}

float SParameterSourceFilter::GetOffset(size_t stream)
{
	if(stream & 1)
		return m_angoffset[stream/2];
	else
		return m_magoffset[stream/2];
}

float SParameterSourceFilter::GetVoltageRange(size_t stream)
{
	if(stream & 1)
		return m_angrange[stream/2];
	else
		return m_magrange[stream/2];
}

void SParameterSourceFilter::SetVoltageRange(float range, size_t stream)
{
	if(stream & 1)
		m_magrange[stream/2] = range;
	else
		m_angrange[stream/2] = range;
}

void SParameterSourceFilter::SetOffset(float offset, size_t stream)
{
	if(stream & 1)
		m_magoffset[stream/2] = offset;
	else
		m_angoffset[stream/2] = offset;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

bool SParameterSourceFilter::IsOverlay()
{
	return false;
}

void SParameterSourceFilter::SetupStreams()
{
	ClearStreams();

	for(size_t to=0; to<m_params.GetNumPorts(); to++)
	{
		for(size_t from=0; from<m_params.GetNumPorts(); from++)
		{
			string param = string("S") + to_string(to+1) + to_string(from+1);

			AddStream(Unit(Unit::UNIT_DB), param + "_mag");
			AddStream(Unit(Unit::UNIT_DEGREES), param + "_ang");
		}
	}

	SetupInitialPortScales();

	m_outputsChangedSignal.emit();
}

void SParameterSourceFilter::SetupInitialPortScales()
{
	//Resize port arrays
	size_t oldsize = m_magrange.size();
	size_t len = m_params.GetNumPorts() * m_params.GetNumPorts();
	m_magrange.resize(len);
	m_magoffset.resize(len);
	m_angrange.resize(len);
	m_angoffset.resize(len);

	//If growing, fill new cells with reasonable default values
	for(size_t i=oldsize; i<len; i++)
	{
		m_magrange[i] = 80;
		m_magoffset[i] = 40;
		m_angrange[i] = 370;
		m_angoffset[i] = 0;
	}
}
