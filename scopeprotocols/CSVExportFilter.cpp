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
#include "CSVExportFilter.h"

#include <cinttypes>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

CSVExportFilter::CSVExportFilter(const string& color)
	: ExportFilter(color)
	, m_inputCount("Columns")
{
	m_parameters[m_fname].m_fileFilterMask = "*.csv";
	m_parameters[m_fname].m_fileFilterName = "Comma Separated Value files (*.csv)";

	m_parameters[m_inputCount] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_inputCount].signal_changed().connect(sigc::mem_fun(*this, &CSVExportFilter::OnColumnCountChanged));
	m_parameters[m_inputCount].SetIntVal(1);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool CSVExportFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	//Reject invalid port indexes
	if(i >= (size_t)m_parameters[m_inputCount].GetIntVal())
		return false;

	//Reject weird stream types that don't make sense as CSV
	switch(stream.GetType())
	{
		case Stream::STREAM_TYPE_ANALOG:
		case Stream::STREAM_TYPE_DIGITAL:
		case Stream::STREAM_TYPE_PROTOCOL:
			return true;

		case Stream::STREAM_TYPE_DIGITAL_BUS:		//TODO: support this

		case Stream::STREAM_TYPE_ANALOG_SCALAR:
		case Stream::STREAM_TYPE_EYE:
		case Stream::STREAM_TYPE_SPECTROGRAM:
		case Stream::STREAM_TYPE_WATERFALL:
		case Stream::STREAM_TYPE_TRIGGER:
		case Stream::STREAM_TYPE_UNDEFINED:
		default:
			return false;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string CSVExportFilter::GetProtocolName()
{
	return "CSV Export";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void CSVExportFilter::Export()
{
	if(!VerifyAllInputsOK())
		return;

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

	//Pre-cast some waveforms so we don't have to do it a lot
	std::vector<SparseWaveformBase*> sparse;
	std::vector<UniformWaveformBase*> uniform;
	std::vector<SparseAnalogWaveform*> sa;
	std::vector<UniformAnalogWaveform*> ua;
	std::vector<SparseDigitalWaveform*> sd;
	std::vector<UniformDigitalWaveform*> ud;
	std::vector<size_t> indexes;
	std::vector<size_t> lens;
	for(size_t i=0; i<GetInputCount(); i++)
	{
		auto data = GetInput(i).GetData();
		sparse.push_back(dynamic_cast<SparseWaveformBase*>(data));
		uniform.push_back(dynamic_cast<UniformWaveformBase*>(data));
		sa.push_back(dynamic_cast<SparseAnalogWaveform*>(data));
		ua.push_back(dynamic_cast<UniformAnalogWaveform*>(data));
		sd.push_back(dynamic_cast<SparseDigitalWaveform*>(data));
		ud.push_back(dynamic_cast<UniformDigitalWaveform*>(data));
		indexes.push_back(0);
		lens.push_back(data->size());
	}

	//Main export path
	int64_t timestamp = INT64_MIN;
	bool first = true;
	while(true)
	{
		//TODO: handle some waveforms starting earlier than others? we should print empty values prior to the first sample
		//TODO: handle gaps between events

		//Find next edge on any input
		int64_t next = INT64_MAX;
		for(size_t i=0; i<GetInputCount(); i++)
			next = min(next, GetNextEventTimestampScaled(sparse[i], uniform[i], indexes[i], lens[i], timestamp));

		//If we can't advance any more, we're done
		if( (next == INT64_MAX) || (next == timestamp) )
			break;

		//First iteration is just indexing
		if(!first)
		{
			//Write timestamp
			if(xunit == Unit(Unit::UNIT_FS))
				fprintf(m_fp, "%.10e", timestamp / FS_PER_SECOND);
			else if(xunit == Unit(Unit::UNIT_HZ))
				fprintf(m_fp, "%" PRId64 "", timestamp);
			else
				fprintf(m_fp, "%" PRId64 "", timestamp);

			//Write values
			for(size_t i=0; i<GetInputCount(); i++)
			{
				switch(GetInput(i).GetType())
				{
					case Stream::STREAM_TYPE_ANALOG:
					fprintf(m_fp, ",%f", GetValue(sa[i], ua[i], indexes[i]));
					break;

					case Stream::STREAM_TYPE_DIGITAL:
						fprintf(m_fp, ",%d", GetValue(sd[i], ud[i], indexes[i]));
						break;

					case Stream::STREAM_TYPE_PROTOCOL:
						if(sparse[i])
							fprintf(m_fp, ",%s", sparse[i]->GetText(indexes[i]).c_str());
						else
							fprintf(m_fp, ",%s", uniform[i]->GetText(indexes[i]).c_str());
						break;

					default:
						fprintf(m_fp, ",%s", "[unimplemented]");
						break;
				}
			}
			fprintf(m_fp, "\n");
		}
		first = false;

		//All good, move on
		timestamp = next;
		for(size_t i=0; i<GetInputCount(); i++)
			AdvanceToTimestampScaled(sparse[i], uniform[i], indexes[i], lens[i], timestamp);
	}
}

void CSVExportFilter::OnColumnCountChanged()
{
	//Close the existing file
	if(m_fp)
		fclose(m_fp);
	m_fp = nullptr;

	//Add new ports
	size_t sizeNew = m_parameters[m_inputCount].GetIntVal();
	size_t sizeOld = m_inputs.size();
	for(size_t i=sizeOld; i<sizeNew; i++)
		CreateInput(string("column") + to_string(i+1));

	//Remove extra ports, if any
	m_inputs.resize(sizeNew);
	m_signalNames.resize(sizeNew);

	//Inputs changed
	signal_inputsChanged().emit();
}
