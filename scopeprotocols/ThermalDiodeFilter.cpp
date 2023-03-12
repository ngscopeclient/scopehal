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
#include "ThermalDiodeFilter.h"
#ifdef __x86_64__
#include <immintrin.h>
#endif

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ThermalDiodeFilter::ThermalDiodeFilter(const string& color)
	: Filter(color, CAT_MEASUREMENT)
	, m_diodeType("Diode type")
{
	AddStream(Unit(Unit::UNIT_CELSIUS), "temp", Stream::STREAM_TYPE_ANALOG);
	CreateInput("VTEMP");

	m_parameters[m_diodeType] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_diodeType].AddEnumValue("LTC3374", DIODE_LTC3374);
	m_parameters[m_diodeType].AddEnumValue("LTC3374A", DIODE_LTC3374A);
	m_parameters[m_diodeType].SetIntVal(DIODE_LTC3374);
}

ThermalDiodeFilter::~ThermalDiodeFilter()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool ThermalDiodeFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if(i >= 1)
		return false;
	if(stream.GetType() != Stream::STREAM_TYPE_ANALOG_SCALAR)
		return false;

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string ThermalDiodeFilter::GetProtocolName()
{
	return "Thermal Diode";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void ThermalDiodeFilter::Refresh(vk::raii::CommandBuffer& /*cmdBuf*/, shared_ptr<QueueHandle> /*queue*/)
{
	m_streams[0].m_stype = Stream::STREAM_TYPE_ANALOG_SCALAR;
	SetData(nullptr, 0);

	float offset = 0;
	float gain = 1;
	switch(m_parameters[m_diodeType].GetIntVal())
	{
		case DIODE_LTC3374:
			offset = 19e-3;
			gain = 1 / 6.75e-3;
			break;

		case DIODE_LTC3374A:
			offset = -45e-3;
			gain = 1 / -7e-3;
			break;

		default:
			break;
	}

	m_streams[0].m_value = (GetInput(0).GetScalarValue() + offset) * gain;
}
