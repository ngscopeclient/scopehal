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
#include "TDRFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

TDRFilter::TDRFilter(const string& color)
	: Filter(color, CAT_ANALYSIS)
	, m_mode(m_parameters["Output Format"])
	, m_portImpedance(m_parameters["Port impedance"])
	, m_stepStartVoltage(m_parameters["Step start"])
	, m_stepEndVoltage(m_parameters["Step end"])
{
	AddStream(Unit(Unit::UNIT_VOLTS), "data", Stream::STREAM_TYPE_ANALOG);
	CreateInput("voltage");

	m_mode = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_mode.AddEnumValue("Reflection coefficient", MODE_RHO);
	m_mode.AddEnumValue("Impedance", MODE_IMPEDANCE);
	m_mode.SetIntVal(MODE_IMPEDANCE);

	m_portImpedance = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_OHMS));
	m_portImpedance.SetFloatVal(50);

	m_stepStartVoltage = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_stepStartVoltage.SetFloatVal(0);

	m_stepEndVoltage = FilterParameter(FilterParameter::TYPE_FLOAT, Unit(Unit::UNIT_VOLTS));
	m_stepEndVoltage.SetFloatVal(1);

	m_oldMode = MODE_IMPEDANCE;
}

TDRFilter::~TDRFilter()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool TDRFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i == 0) && (stream.GetType() == Stream::STREAM_TYPE_ANALOG) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string TDRFilter::GetProtocolName()
{
	return "TDR";
}

void TDRFilter::SetDefaultName()
{
	char hwname[256];
	if(m_mode.GetEnumVal<OutputMode>() == MODE_IMPEDANCE)
		snprintf(hwname, sizeof(hwname), "TDRImpedance(%s)", GetInputDisplayName(0).c_str());
	else
		snprintf(hwname, sizeof(hwname), "TDRReflection(%s)", GetInputDisplayName(0).c_str());

	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void TDRFilter::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue
	)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("TDRFilter::Refresh");
	#endif
	ClearErrors();

	auto din = dynamic_cast<UniformAnalogWaveform*>(GetInputWaveform(0));
	if(!din)
	{
		AddErrorMessage("Missing input", "One or more inputs are unconnected");
		SetData(nullptr, 0);
		return;
	}

	auto len = din->size();

	//Extract parameters
	auto mode = m_mode.GetEnumVal<OutputMode>();
	auto z0 = m_portImpedance.GetFloatVal();
	auto vlo = m_stepStartVoltage.GetFloatVal();
	auto vhi = m_stepEndVoltage.GetFloatVal();
	auto pulseAmplitude = (vhi - vlo);

	//Set up units
	if(mode == MODE_IMPEDANCE)
		SetYAxisUnits(Unit(Unit::UNIT_OHMS), 0);
	else
		SetYAxisUnits(Unit(Unit::UNIT_RHO), 0);

	//Set up the output waveform
	auto cap = SetupEmptyUniformAnalogOutputWaveform(din, 0);
	cap->Resize(len);
	cap->PrepareForCpuAccess();

	float pulseScale = 1.0 / pulseAmplitude;
	for(size_t i=0; i<len; i++)
	{
		//Reflection coefficient is trivial
		float rho = (din->m_samples[i] - vhi) * pulseScale;

		//Impedance takes a bit more work to calculate
		if(mode == MODE_IMPEDANCE)
			cap->m_samples[i] = z0 * (1 + rho)/(1 - rho);

		else
			cap->m_samples[i] = rho;
	}

	//Reset gain/offset if output mode was changed
	if(mode != m_oldMode)
	{
		AutoscaleVertical(0);
		m_oldMode = mode;
	}

	cap->MarkModifiedFromCpu();
}
