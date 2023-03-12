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

	//TODO

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
	auto portCount = m_parameters[m_portCount].GetIntVal();
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
	if(!VerifyAllInputsOK())
		return;

	//Touchstone files don't support appending, that makes no sense. So always close and rewrite the file
	Clear();

	/*
	//If file is not open, open it and write a header row
	Unit xunit = GetInput(0).GetXAxisUnits();
	if(!m_fp)
	{
		auto mode = static_cast<ExportMode_t>(m_parameters[m_mode].GetIntVal());

		bool append = (mode == MODE_CONTINUOUS_APPEND) || (mode == MODE_MANUAL_APPEND);
		if(append)
			m_fp = fopen(m_parameters[m_fname].GetFileName().c_str(), "ab");
		else
			m_fp = fopen(m_parameters[m_fname].GetFileName().c_str(), "wb");

		//See if file is empty. If so, write header
		fseek(m_fp, 0, SEEK_END);
		if(ftell(m_fp) == 0)
		{
			if(xunit == Unit(Unit::UNIT_FS))
				fprintf(m_fp, "Time (s)");
			else if(xunit == Unit(Unit::UNIT_HZ))
				fprintf(m_fp, "Frequency (Hz)");
			else
				fprintf(m_fp, "X Unit");

			//Write other fields
			for(size_t i=0; i<GetInputCount(); i++)
			{
				string colname = GetInput(i).GetName();
				colname = str_replace(",", "_", colname);
				fprintf(m_fp, ",%s", colname.c_str());
			}
			fprintf(m_fp, "\n");
		}
	}
	*/
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
