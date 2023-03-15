/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2023 Andrew D. Zonenberg and contributors                                                         *
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
#include "ScalarStairstepFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ScalarStairstepFilter::ScalarStairstepFilter(const string& color)
	: Filter(color, CAT_GENERATION)
	, m_start("Begin")
	, m_end("End")
	, m_interval("Step interval")
	, m_nsteps("Step count")
	, m_unit("Unit")
	, m_lastUpdate(GetTime())
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG_SCALAR);

	m_parameters[m_start] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_start].SetFloatVal(0);

	m_parameters[m_end] = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_parameters[m_end].SetFloatVal(1);

	m_parameters[m_interval] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));
	m_parameters[m_interval].SetFloatVal(FS_PER_SECOND);

	m_parameters[m_nsteps] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_nsteps].SetIntVal(10);

	m_parameters[m_unit] = FilterParameter::UnitSelector();
	m_parameters[m_unit].SetIntVal(Unit::UNIT_VOLTS);

	SetData(nullptr, 0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool ScalarStairstepFilter::ValidateChannel(size_t /*i*/, StreamDescriptor /*stream*/)
{
	//no inputs
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string ScalarStairstepFilter::GetProtocolName()
{
	return "Scalar Stairstep";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void ScalarStairstepFilter::Refresh(vk::raii::CommandBuffer& /*cmdBuf*/, shared_ptr<QueueHandle> /*queue*/)
{
	SetYAxisUnits(static_cast<Unit::UnitType>(m_parameters[m_unit].GetIntVal()), 0);

	//See how long it's been since our last update
	double now = GetTime();
	double timeOfNextUpdate = m_lastUpdate + m_parameters[m_interval].GetFloatVal()*SECONDS_PER_FS;
	if(timeOfNextUpdate > now)
		return;

	//Time to update!
	//Backdate our nominal update time to the exact interval
	//so graph execution times don't cause skew of future updates
	m_lastUpdate = timeOfNextUpdate;

	//Are we at the last step? If so, wrap back to the start
	float start = m_parameters[m_start].GetFloatVal();
	float end = m_parameters[m_end].GetFloatVal();
	float delta = end - start;
	float stepsize = delta / m_parameters[m_nsteps].GetIntVal();
	if( fabs(m_streams[0].m_value - end) < (0.5*stepsize) )
		m_streams[0].m_value = start;

	//Otherwise, bump
	else
		m_streams[0].m_value += stepsize;
}
