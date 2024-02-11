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
#include "TouchstoneExportFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

TouchstoneExportFilter::TouchstoneExportFilter(const string& color)
	: ExportFilter(color)
	, m_portCount("Ports")
	, m_freqUnit("Frequency unit")
	, m_format("Format")
{
	m_parameters[m_fname].m_fileFilterMask = "*.s*p";
	m_parameters[m_fname].m_fileFilterName = "Touchstone S-parameter files (*.s*p)";

	m_parameters[m_portCount] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_portCount].signal_changed().connect(sigc::mem_fun(*this, &TouchstoneExportFilter::OnPortCountChanged));
	m_parameters[m_portCount].SetIntVal(2);

	m_parameters[m_freqUnit] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_freqUnit].AddEnumValue("Hz", SParameters::FREQ_HZ);
	m_parameters[m_freqUnit].AddEnumValue("kHz", SParameters::FREQ_KHZ);
	m_parameters[m_freqUnit].AddEnumValue("MHz", SParameters::FREQ_MHZ);
	m_parameters[m_freqUnit].AddEnumValue("GHz", SParameters::FREQ_GHZ);
	m_parameters[m_freqUnit].SetIntVal(SParameters::FREQ_MHZ);

	m_parameters[m_format] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_format].AddEnumValue("Mag / angle", SParameters::FORMAT_MAG_ANGLE);
	m_parameters[m_format].AddEnumValue("dB / angle", SParameters::FORMAT_DBMAG_ANGLE);
	m_parameters[m_format].AddEnumValue("Real / imaginary", SParameters::FORMAT_REAL_IMAGINARY);
	m_parameters[m_format].SetIntVal(SParameters::FORMAT_MAG_ANGLE);

	OnPortCountChanged();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool TouchstoneExportFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if(stream.GetType() != Stream::STREAM_TYPE_ANALOG)
		return false;

	//Validate port index
	size_t portCount = m_parameters[m_portCount].GetIntVal();
	if(i >= (portCount*portCount*2) )
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string TouchstoneExportFilter::GetProtocolName()
{
	return "Touchstone Export";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void TouchstoneExportFilter::Export()
{
	LogTrace("Exporting Touchstone data to %s\n", m_parameters[m_fname].GetFileName().c_str());
	LogIndenter li;

	//Touchstone files don't support appending, that makes no sense. So always close and rewrite the file
	Clear();

	//Create the output parameters
	auto nports = m_parameters[m_portCount].GetIntVal();
	SParameters params;
	params.Allocate(nports);

	//Convert from display oriented dB/degrees to linear magnitude / radians (internal SParameters class format).
	//This then gets converted to whatever we need in the actual Touchstone file.
	//For now, assume all inputs have the same frequency spacing etc.
	//TODO: detect this and print error or (ideally) resample
	for(int to=0; to < nports; to++)
	{
		for(int from=0; from < nports; from++)
		{
			auto base = to*nports + from;
			auto mdata = GetInput(base*2).GetData();
	 		auto adata = GetInput(base*2 + 1).GetData();

			auto umagData = dynamic_cast<const UniformAnalogWaveform*>(mdata);
			auto uangData = dynamic_cast<const UniformAnalogWaveform*>(adata);

			auto smagData = dynamic_cast<const SparseAnalogWaveform*>(mdata);
			auto sangData = dynamic_cast<const SparseAnalogWaveform*>(adata);

			if(umagData && uangData)
				params[SPair(to+1, from+1)].ConvertFromWaveforms(umagData, uangData);
			else if(smagData && sangData)
				params[SPair(to+1, from+1)].ConvertFromWaveforms(smagData, sangData);

			//Missing data, fill it out with zeroes at the same frequency spacing
			else
			{
				LogTrace("No data for S%d%d, zero filling\n", to+1, from+1);

				bool hit = false;
				for(int refto=0; refto < nports; refto++)
				{
					for(int reffrom=0; reffrom < nports; reffrom++)
					{
						auto refbase = refto*nports + reffrom;
						auto refmdata = GetInput(refbase*2).GetData();
						auto refadata = GetInput(refbase*2 + 1).GetData();

						auto refumagData = dynamic_cast<const UniformAnalogWaveform*>(refmdata);
						auto refuangData = dynamic_cast<const UniformAnalogWaveform*>(refadata);

						auto refsmagData = dynamic_cast<const SparseAnalogWaveform*>(refmdata);
						auto refsangData = dynamic_cast<const SparseAnalogWaveform*>(refadata);

						if(refumagData && refuangData)
							params[SPair(to+1, from+1)].ZeroFromWaveforms(refumagData, refuangData);
						else if(refsmagData && refsangData)
							params[SPair(to+1, from+1)].ZeroFromWaveforms(refsmagData, refsangData);
						else
							continue;

						hit = true;
						break;
					}
				}

				//nothing found, stop
				if(!hit)
					params[SPair(to+1, from+1)].clear();


				continue;
			}
		}
	}

	auto format = static_cast<SParameters::ParameterFormat>(m_parameters[m_format].GetIntVal());
	auto freqUnit = static_cast<SParameters::FreqUnit>(m_parameters[m_freqUnit].GetIntVal());

	//Done, save it
	params.SaveToFile(
		m_parameters[m_fname].GetFileName(),
		format,
		freqUnit);
}

void TouchstoneExportFilter::OnPortCountChanged()
{
	//Add new ports
	size_t portCount = m_parameters[m_portCount].GetIntVal();
	size_t sizeNew = portCount * portCount*2;
	size_t sizeOld = m_inputs.size();
	for(size_t i=sizeOld; i<sizeNew; i++)
		CreateInput("xx");

	//Rename ports
	for(size_t to=0; to < portCount; to++)
	{
		for(size_t from=0; from < portCount; from++)
		{
			auto base = to*portCount + from;
			m_signalNames[base*2 + 0] = string("S") + to_string(to+1) + to_string(from+1) + "_mag";
			m_signalNames[base*2 + 1] = string("S") + to_string(to+1) + to_string(from+1) + "_ang";
		}
	}

	//Remove extra ports, if any
	m_inputs.resize(sizeNew);
	m_signalNames.resize(sizeNew);

	//Inputs changed
	signal_inputsChanged().emit();
}
