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
#include "BINImportFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

BINImportFilter::BINImportFilter(const string& color)
	: ImportFilter(color)
{
	m_fpname = "BIN File";
	m_parameters[m_fpname] = FilterParameter(FilterParameter::TYPE_FILENAME, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_fpname].m_fileFilterMask = "*.bin";
	m_parameters[m_fpname].m_fileFilterName = "Agilent / Keysight / Rigol binary waveform files (*.bin)";
	m_parameters[m_fpname].signal_changed().connect(sigc::mem_fun(*this, &BINImportFilter::OnFileNameChanged));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string BINImportFilter::GetProtocolName()
{
	return "BIN Import";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void BINImportFilter::OnFileNameChanged()
{
	//Wipe anything we may have had in the past
	ClearStreams();

	auto fname = m_parameters[m_fpname].ToString();
	if(fname.empty())
		return;

	//Set waveform timestamp to file timestamp
	time_t timestamp = 0;
	int64_t fs = 0;
	GetTimestampOfFile(fname, timestamp, fs);

	string f = ReadFile(fname);
	uint32_t fpos = 0;

	FileHeader fh;
	f.copy((char*)&fh, sizeof(FileHeader), fpos);
	fpos += sizeof(FileHeader);

	//Get vendor from file signature
	string vendor;
	switch(fh.magic[0])
	{
		case 'A':
			vendor = "Agilent/Keysight";
			break;

		case 'R':
			vendor = "Rigol";
			break;

		default:
			LogError("Unknown vendor in file header");
			return;
	}
	LogDebug("Vendor:    %s\n", vendor.c_str());
	//LogDebug("File size: %i bytes\n", fh.length);
	LogDebug("Waveforms: %i\n\n", fh.count);

	//Process each stream in the file
	string hwname;
	string serial;
	for(size_t i=0; i<fh.count; i++)
	{
		LogDebug("Waveform %i:\n", (int)i+1);
		LogIndenter li_w;

		//Parse waveform header
		WaveHeader wh;
		f.copy((char*)&wh, sizeof(WaveHeader), fpos);
		fpos += sizeof(WaveHeader);

		//TODO: make this metadata readable somewhere via properties etc
		if (i == 0)
		{
			// Split hardware string
			int idx = 0;
			for(int c=0; c<24; c++)
			{
				if(wh.hardware[c] == ':')
				{
					idx = c;
					break;
				}
			}

			//Set oscilloscope metadata
			hwname.assign(wh.hardware, idx);
			serial.assign(wh.hardware + idx + 1, 24 - idx);
		}

		//Create output stream
		string name = wh.label;
		if(name == "")
			name = string("CH") + to_string(i+1);

		LogDebug("Samples:      %i\n", wh.samples);
		LogDebug("Buffers:      %i\n", wh.buffers);
		LogDebug("Type:         %i\n", wh.type);
		LogDebug("Duration:     %.*f us\n", 2, wh.duration * 1e6);
		LogDebug("Start:        %.*f us\n", 2, wh.start * 1e6);
		LogDebug("Interval:     %.*f ns\n", 2, wh.interval * 1e9);
		LogDebug("Origin:       %.*f us\n", 2, wh.origin * 1e6);
		LogDebug("Holdoff:      %.*f ms\n", 2, wh.holdoff * 1e3);
		LogDebug("Sample Rate:  %.*f Msps\n", 2, (1 / wh.interval) / 1e6);
		LogDebug("Frame:        %s\n", hwname.c_str());
		LogDebug("Label:        %s\n", name.c_str());
		LogDebug("Serial:       %s\n\n", serial.c_str());

		//Grab the initial data header and figure out what it is
		DataHeader dh;
		f.copy((char*)&dh, sizeof(DataHeader), fpos);

		//Digital logic waveform
		if(wh.type == 6)
		{
			//Create 8 streams of digital data
			vector<UniformDigitalWaveform*> wfms;
			for(size_t j=0; j<8; j++)
			{
				AddStream(Unit(Unit::UNIT_VOLTS), name + "[" + to_string(j) + "]", Stream::STREAM_TYPE_DIGITAL);

				auto wfm = new UniformDigitalWaveform;
				wfm->m_timescale = wh.interval * FS_PER_SECOND;
				wfm->m_startTimestamp = timestamp;
				wfm->m_startFemtoseconds = fs;
				wfm->m_triggerPhase = 0;
				SetData(wfm, m_streams.size()-1);
				wfms.push_back(wfm);

				wfm->PrepareForCpuAccess();
			}

			for(size_t j=0; j<wh.buffers; j++)
			{
				LogDebug("Buffer %i:\n", (int)j+1);
				LogIndenter li_b;

				//Parse waveform data header
				f.copy((char*)&dh, sizeof(DataHeader), fpos);
				fpos += sizeof(DataHeader);

				LogDebug("Data Type:      %i\n", dh.type);
				LogDebug("Sample depth:   %i bits\n", dh.depth*8);
				LogDebug("Buffer length:  %i KB\n\n\n", dh.length/1024);

				for(size_t k=0; k<wh.samples; k++)
				{
					uint8_t s = -1;

					//Logic samples (counts 32-bit float data waveforms)
					if (dh.type == 5)
					{
						//Do not violate strict aliasing, compiler will optimize out the memcpy
						float val;
						memcpy(&val, f.c_str() + fpos, sizeof(float));
						s = static_cast<uint8_t>(val);
					}
					//Logic samples (digital unsigned 8-bit character data)
					else if (dh.type == 6)
					{
						s = *(uint8_t*)(f.c_str() + fpos);
					}
					else
					{
						LogDebug("Invalid buffer type for logic waveform\n");
						return;
					}

					for(size_t m=0; m<8; m++)
					{
						if(s & (1 << m) )
							wfms[m]->m_samples.push_back(true);
						else
							wfms[m]->m_samples.push_back(false);
					}

					fpos += dh.depth;
				}
			}

			for(auto w : wfms)
				w->MarkModifiedFromCpu();
		}

		//Analog waveform
		else
		{
			//Create the stream and a waveform for it
			AddStream(Unit(Unit::UNIT_VOLTS), name, Stream::STREAM_TYPE_ANALOG);

			auto wfm = new UniformAnalogWaveform;
			wfm->m_timescale = wh.interval * FS_PER_SECOND;
			wfm->m_startTimestamp = timestamp;
			wfm->m_startFemtoseconds = fs;
			wfm->m_triggerPhase = 0;
			wfm->PrepareForCpuAccess();
			SetData(wfm, m_streams.size()-1);

			for(size_t j=0; j<wh.buffers; j++)
			{
				LogDebug("Buffer %i:\n", (int)j+1);
				LogIndenter li_b;

				//Parse waveform data header
				f.copy((char*)&dh, sizeof(DataHeader), fpos);
				fpos += sizeof(DataHeader);

				LogDebug("Data Type:      %i\n", dh.type);
				LogDebug("Sample depth:   %i bits\n", dh.depth*8);
				LogDebug("Buffer length:  %i KB\n\n\n", dh.length/1024);

				//Float samples (analog waveforms)
				for(size_t k=0; k<wh.samples; k++)
				{
					//Do not violate strict aliasing, compiler will optimize out the memcpy
					float* sample_f;
					memcpy(&sample_f, f.c_str() + fpos, sizeof(float));
					wfm->m_samples.push_back(*sample_f);
					fpos += dh.depth;
				}
			}

			wfm->MarkModifiedFromCpu();
		}

		AutoscaleVertical(i);
	}

	m_outputsChangedSignal.emit();
}
