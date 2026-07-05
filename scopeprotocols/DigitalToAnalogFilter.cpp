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
#include "DigitalToAnalogFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DigitalToAnalogFilter::DigitalToAnalogFilter(const string& color)
	: Filter(color, CAT_MATH)
	, m_gain(m_parameters["Gain"])
	, m_offset(m_parameters["Offset"])
	, m_unit(m_parameters["Unit"])
	, m_mode(m_parameters["Mode"])
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG_SCALAR);

	CreateInput<InputConstraintStreamType>("din", Stream::STREAM_TYPE_DIGITAL_SCALAR);

	m_gain = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_gain.SetFloatVal(1);

	m_offset = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_offset.SetFloatVal(0);

	m_unit = FilterParameter::UnitSelector();
	m_unit.SetIntVal(Unit::UNIT_VOLTS);
	m_unit.signal_changed().connect(sigc::mem_fun(*this, &DigitalToAnalogFilter::OnUnitChanged));

	m_mode = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_mode.AddEnumValue("Unsigned normalized", MODE_UNSIGNED_NORMALIZED);
	m_mode.AddEnumValue("Unsigned", MODE_UNSIGNED);
	m_mode.SetIntVal(MODE_UNSIGNED_NORMALIZED);

	SetData(nullptr, 0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string DigitalToAnalogFilter::GetProtocolName()
{
	return "Digital to Analog";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DigitalToAnalogFilter::OnUnitChanged()
{
	//TODO preserve values
	m_gain = FilterParameter(FilterParameter::TYPE_FLOAT, m_unit.GetEnumVal<Unit::UnitType>());
	m_offset = FilterParameter(FilterParameter::TYPE_FLOAT, m_unit.GetEnumVal<Unit::UnitType>());
}

void DigitalToAnalogFilter::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	SetYAxisUnits(m_unit.GetEnumVal<Unit::UnitType>(), 0);

	auto din = GetInput(0);
	ClearErrors();
	if(!din)
	{
		AddErrorMessage("Missing inputs", "No signal input connected");
		return;
	}

	auto inval = din.GetDigitalScalarValue();
	auto width = din.GetDigitalWidth();

	//TODO: signed value path, for now assume unsigned

	auto mode = m_mode.GetEnumVal<mode_t>();

	//Get the full-scale value
	uint64_t fullscale = 0;
	fullscale = ~fullscale;
	fullscale <<= width;
	fullscale = ~fullscale;

	//Normalize and scale the input
	//TODO: option to not normalize
	double norm = static_cast<double>(inval);
	if(mode == MODE_UNSIGNED_NORMALIZED)
		norm /= static_cast<double>(fullscale);
	norm -= m_offset.GetFloatVal();
	norm *= m_gain.GetFloatVal();

	//Save the output
	m_streams[0].m_value = norm;
}
