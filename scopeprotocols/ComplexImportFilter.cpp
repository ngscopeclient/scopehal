/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2022 Andrew D. Zonenberg and contributors                                                         *
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
#include "ComplexImportFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ComplexImportFilter::ComplexImportFilter(const string& color)
	: ImportFilter(color)
	, m_formatname("File Format")
	, m_sratename("Sample Rate")
{
	m_fpname = "Complex File";
	m_parameters[m_fpname] = FilterParameter(FilterParameter::TYPE_FILENAME, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_fpname].m_fileFilterMask = "*.complex";
	m_parameters[m_fpname].m_fileFilterName = "Complex files (*.complex)";
	m_parameters[m_fpname].signal_changed().connect(sigc::mem_fun(*this, &ComplexImportFilter::Reload));

	m_parameters[m_formatname] = FilterParameter(FilterParameter::TYPE_ENUM, Unit::UNIT_COUNTS);
	m_parameters[m_formatname].AddEnumValue("Integer (8 bit unsigned)", FORMAT_UNSIGNED_INT8);
	m_parameters[m_formatname].AddEnumValue("Integer (8 bit signed)", FORMAT_SIGNED_INT8);
	m_parameters[m_formatname].AddEnumValue("Integer (16 bit signed)", FORMAT_SIGNED_INT16);
	m_parameters[m_formatname].AddEnumValue("Floating point (32 bit single precision)", FORMAT_FLOAT32);
	m_parameters[m_formatname].AddEnumValue("Floating point (64 bit double precision)", FORMAT_FLOAT64);
	m_parameters[m_formatname].SetIntVal(FORMAT_SIGNED_INT8);
	m_parameters[m_formatname].signal_changed().connect(sigc::mem_fun(*this, &ComplexImportFilter::Reload));

	m_parameters[m_sratename] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLERATE));
	m_parameters[m_sratename].SetIntVal(1e6);
	m_parameters[m_sratename].signal_changed().connect(sigc::mem_fun(*this, &ComplexImportFilter::Reload));

	ClearStreams();
	AddStream(Unit(Unit::UNIT_VOLTS), "I");
	AddStream(Unit(Unit::UNIT_VOLTS), "Q");

	m_ranges.push_back(2);
	m_ranges.push_back(2);

	m_offsets.push_back(0);
	m_offsets.push_back(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string ComplexImportFilter::GetProtocolName()
{
	return "Complex Import";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void ComplexImportFilter::Reload()
{
	auto fname = m_parameters[m_fpname].ToString();
	if(fname.empty())
		return;

	//Set waveform timestamp to file timestamp
	time_t timestamp = 0;
	int64_t fs = 0;
	GetTimestampOfFile(fname, timestamp, fs);

	//Load the file
	FILE* fp = fopen(fname.c_str(), "rb");
	if(!fp)
	{
		LogError("Couldn't open complex file \"%s\"\n", fname.c_str());
		return;
	}
	fseek(fp, 0, SEEK_END);
	size_t len_bytes = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	uint8_t* buf = new uint8_t[len_bytes];
	if(len_bytes != fread(buf, 1, len_bytes, fp))
	{
		LogError("Failed to read complex data\n");
		fclose(fp);
		return;
	}
	fclose(fp);
	fp = NULL;

	//Create new waveforms
	int64_t samplerate = m_parameters[m_sratename].GetIntVal();
	if(samplerate == 0)
		return;
	int64_t interval = FS_PER_SECOND / samplerate;

	auto iwfm = new AnalogWaveform;
	iwfm->m_timescale = interval;
	iwfm->m_startTimestamp = timestamp;
	iwfm->m_startFemtoseconds = fs;
	iwfm->m_triggerPhase = 0;
	iwfm->m_densePacked = true;
	SetData(iwfm, 0);

	auto qwfm = new AnalogWaveform;
	qwfm->m_timescale = interval;
	qwfm->m_startTimestamp = timestamp;
	qwfm->m_startFemtoseconds = fs;
	qwfm->m_triggerPhase = 0;
	qwfm->m_densePacked = true;
	SetData(qwfm, 1);

	//Figure out actual data element size
	auto fmt = static_cast<Format>(m_parameters[m_formatname].GetIntVal());
	int bytes_per_sample = 1;
	switch(fmt)
	{
		case FORMAT_UNSIGNED_INT8:
		case FORMAT_SIGNED_INT8:
			bytes_per_sample = 1;
			break;

		case FORMAT_SIGNED_INT16:
			bytes_per_sample = 2;
			break;

		case FORMAT_FLOAT32:
			bytes_per_sample = 4;
			break;

		case FORMAT_FLOAT64:
			bytes_per_sample = 8;
			break;
	}
	size_t nsamples = len_bytes / (bytes_per_sample * 2);

	//Fill duration/offset of samples
	//TODO: vectorize this?
	iwfm->Resize(nsamples);
	qwfm->Resize(nsamples);
	for(size_t i=0; i<nsamples; i++)
	{
		iwfm->m_offsets[i] = i;
		qwfm->m_offsets[i] = i;

		iwfm->m_durations[i] = 1;
		qwfm->m_durations[i] = 1;
	}

	//Actual output processing
	//TODO: vectorize this?
	switch(fmt)
	{
		case FORMAT_UNSIGNED_INT8:
			{
				float scale = 1.0f / 127.0f;
				auto wfm = reinterpret_cast<uint8_t*>(buf);
				for(size_t i=0; i<nsamples; i++)
				{
					iwfm->m_samples[i]	= (wfm[i*2] - 128) * scale;
					qwfm->m_samples[i]	= (wfm[i*2 + 1] - 128) * scale;
				}
			}
			break;

		case FORMAT_SIGNED_INT8:
			{
				float scale = 1.0f / 127.0f;
				auto wfm = reinterpret_cast<int8_t*>(buf);
				for(size_t i=0; i<nsamples; i++)
				{
					iwfm->m_samples[i]	= wfm[i*2] * scale;
					qwfm->m_samples[i]	= wfm[i*2 + 1] * scale;
				}
			}
			break;

		case FORMAT_SIGNED_INT16:
			{
				float scale = 1.0f / 32767.0f;
				auto wfm = reinterpret_cast<int16_t*>(buf);
				for(size_t i=0; i<nsamples; i++)
				{
					iwfm->m_samples[i]	= wfm[i*2] * scale;
					qwfm->m_samples[i]	= wfm[i*2 + 1] * scale;
				}
			}
			break;

		case FORMAT_FLOAT32:
			{
				auto wfm = reinterpret_cast<float*>(buf);
				for(size_t i=0; i<nsamples; i++)
				{
					iwfm->m_samples[i]	= wfm[i*2];
					qwfm->m_samples[i]	= wfm[i*2 + 1];
				}
			}
			break;

		case FORMAT_FLOAT64:
			{
				auto wfm = reinterpret_cast<double*>(buf);
				for(size_t i=0; i<nsamples; i++)
				{
					iwfm->m_samples[i]	= wfm[i*2];
					qwfm->m_samples[i]	= wfm[i*2 + 1];
				}
			}
			break;

	}

	//Done, clean up
	delete[] buf;
}
