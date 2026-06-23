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
#include "ScalarStairstepFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ScalarStairstepFilter::ScalarStairstepFilter(const string& color)
	: Filter(color, CAT_GENERATION)
	, m_start(m_parameters["Begin"])
	, m_end(m_parameters["End"])
	, m_interval(m_parameters["Step interval"])
	, m_nsteps(m_parameters["Step count"])
	, m_unit(m_parameters["Unit"])
	, m_lastUpdate(GetTime())
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG_SCALAR);
	AddStream(Unit(Unit::UNIT_COUNTS), "updated", Stream::STREAM_TYPE_ANALOG_SCALAR);

	m_start = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_start.SetFloatVal(0);

	m_end = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_end.SetFloatVal(1);

	m_interval = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));
	m_interval.SetIntVal(FS_PER_SECOND);

	m_nsteps = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_nsteps.SetIntVal(10);

	m_unit = FilterParameter::UnitSelector();
	m_unit.SetIntVal(Unit::UNIT_VOLTS);
	m_unit.signal_changed().connect(sigc::mem_fun(*this, &ScalarStairstepFilter::OnUnitChanged));

	SetData(nullptr, 0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string ScalarStairstepFilter::GetProtocolName()
{
	return "Scalar Stairstep";
}

vector<string> ScalarStairstepFilter::EnumActions()
{
	vector<string> ret;
	ret.push_back("Restart");
	return ret;
}

bool ScalarStairstepFilter::PerformAction(const string& id)
{
	if(id == "Restart")
	{
		//Trigger an update immediately and set our output
		m_lastUpdate = GetTime();
		m_streams[1].m_value = 1;
		m_streams[0].m_value = m_start.GetFloatVal();
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void ScalarStairstepFilter::OnUnitChanged()
{
	auto unit = m_unit.GetEnumVal<Unit::UnitType>();

	//Don't touch anything if our unit is already the same
	if(m_start.GetUnit() == unit)
		return;

	auto oldstart = m_start.GetFloatVal();
	auto oldend = m_end.GetFloatVal();

	m_start = FilterParameter(FilterParameter::TYPE_FLOAT, unit);
	m_start.SetFloatVal(oldstart);

	m_end = FilterParameter(FilterParameter::TYPE_FLOAT, unit);
	m_end.SetFloatVal(oldend);
}

void ScalarStairstepFilter::LoadParameters(const YAML::Node& node, IDTable& table)
{
	//Do two passes of loading
	//First of base class to set unit, second to configure everything else
	FlowGraphNode::LoadParameters(node, table);
	Filter::LoadParameters(node, table);
}

void ScalarStairstepFilter::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("ScalarStairstepFilter::Refresh");
	#endif
	ClearErrors();

	SetYAxisUnits(m_unit.GetEnumVal<Unit::UnitType>(), 0);

	//See how long it's been since our last update and set update flag accordingly
	double now = GetTime();
	double dt = m_interval.GetFloatVal()*SECONDS_PER_FS;
	double timeOfNextUpdate = m_lastUpdate + dt;
	if(timeOfNextUpdate > now)
	{
		m_streams[1].m_value = 0;
		return;
	}
	m_streams[1].m_value = 1;

	//Time to update!
	//Backdate our nominal update time to the exact interval
	//so graph execution times don't cause skew of future updates.
	//(but don't allow shifting by more than one delta)
	double tlate = timeOfNextUpdate - now;
	if(tlate > (2*dt))
		m_lastUpdate = now;
	else
		m_lastUpdate = timeOfNextUpdate;

	float start = m_start.GetFloatVal();
	float end = m_end.GetFloatVal();
	float delta = end - start;
	float stepsize = delta / m_nsteps.GetIntVal();

	//Clip out of range values
	if((end > start) && ( (m_streams[0].m_value > end) || (m_streams[0].m_value < start) ) )
		m_streams[0].m_value = start;
	else if((end < start) && ( (m_streams[0].m_value < end) || (m_streams[0].m_value > start) ) )
		m_streams[0].m_value = start;

	//Are we at the last step? If so, wrap back to the start
	if( fabs(m_streams[0].m_value - end) < (0.5*stepsize) )
		m_streams[0].m_value = start;

	//Otherwise, bump
	else
		m_streams[0].m_value += stepsize;
}
