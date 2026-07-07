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
#include "SParameterSourceFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SParameterSourceFilter::SParameterSourceFilter(const string& color, Category cat)
	: Filter(color, cat, Unit(Unit::UNIT_HZ))
{
	SetupStreams();
}

SParameterSourceFilter::~SParameterSourceFilter()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void SParameterSourceFilter::SetupStreams()
{
	size_t nports = m_params.GetNumPorts();

	auto nstreams = nports * nports * 2;
	m_streams.resize(nstreams);
	m_sinks.resize(nstreams);
	m_ranges.resize(0);
	m_offsets.resize(0);

	for(size_t to=0; to < nports; to++)
	{
		for(size_t from=0; from < nports; from++)
		{
			string param = string("S") + to_string(to+1) + to_string(from+1);

			size_t base = (to*nports + from) * 2;

			m_streams[base + 0].m_name = param + "_mag";
			m_streams[base + 0].m_stype = Stream::STREAM_TYPE_ANALOG;
			m_streams[base + 0].m_yAxisUnit = Unit(Unit::UNIT_DB);

			m_streams[base + 1].m_name = param + "_ang";
			m_streams[base + 1].m_stype = Stream::STREAM_TYPE_ANALOG;
			m_streams[base + 1].m_yAxisUnit = Unit(Unit::UNIT_DEGREES);
		}
	}

	m_outputsChangedSignal.emit();
}
