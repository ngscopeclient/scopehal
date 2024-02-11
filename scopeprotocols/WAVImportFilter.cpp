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
#include "WAVImportFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

WAVImportFilter::WAVImportFilter(const string& color)
	: ImportFilter(color)
{
	m_fpname = "WAV File";

	m_parameters[m_fpname] = FilterParameter(FilterParameter::TYPE_FILENAME, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_fpname].m_fileFilterMask = "*.wav";
	m_parameters[m_fpname].m_fileFilterName = "WAV files (*.wav)";
	m_parameters[m_fpname].signal_changed().connect(sigc::mem_fun(*this, &WAVImportFilter::OnFileNameChanged));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string WAVImportFilter::GetProtocolName()
{
	return "WAV Import";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void WAVImportFilter::OnFileNameChanged()
{
	auto fname = m_parameters[m_fpname].ToString();
	if(fname.empty())
		return;

	//Set waveform timestamp to file timestamp
	time_t timestamp = 0;
	int64_t fs = 0;
	GetTimestampOfFile(fname, timestamp, fs);

	FILE* fp = fopen(fname.c_str(), "rb");
	if(!fp)
	{
		LogError("Couldn't open WAV file \"%s\"\n", fname.c_str());
		return;
	}

	//Read the RIFF tag, should be "RIFF", uint32 len, "WAVE", then data
	uint32_t header[3];
	if(3 != fread(header, sizeof(uint32_t), 3, fp))
	{
		LogError("Failed to read RIFF header\n");
		fclose(fp);
		return;
	}
	if(header[0] != 0x46464952)	// "RIFF"
	{
		LogError("Bad top level chunk type (not a RIFF file)\n");
		fclose(fp);
		return;
	}
	if(header[2] != 0x45564157)	// "WAVE"
	{
		LogError("Bad WAVE data type (not a WAV file)\n");
		fclose(fp);
		return;
	}
	//Ignore RIFF length, it should encompass the entire file

	//Read the format chunk header
	if(2 != fread(header, sizeof(uint32_t), 2, fp))
	{
		LogError("Failed to read format header\n");
		fclose(fp);
		return;
	}
	if(header[0] != 0x20746d66)	// "FMT "
	{
		LogError("Bad WAV format chunk type (not FMT)\n");
		fclose(fp);
		return;
	}
	if( (header[1] < 16) || (header[1] > 128) )
	{
		LogError("Bad WAV format length (expected >= 16 and <= 128)\n");
		fclose(fp);
		return;
	}

	//Read the format
	uint8_t format[128];
	if(header[1] != fread(format, 1, header[1], fp))
	{
		LogError("Failed to read format\n");
		fclose(fp);
		return;
	}

	// Do not violate strict aliasing, compiler will optimize out the memcpy's
	uint16_t afmt, nchans, nbits;
	uint32_t srate;
	memcpy(&afmt, format, sizeof(uint16_t));
	memcpy(&nchans, format+2, sizeof(uint16_t));
	memcpy(&srate, format+4, sizeof(uint32_t));
	memcpy(&nbits, format+14, sizeof(uint16_t));

	//Ignore any extensions to the format header

	//1 = integer PCM, 3 = ieee754 float
	if(afmt == 1)
	{
		//TODO: support int24?
		if( (nbits != 8) && (nbits != 16) )
		{
			LogError(
				"Integer PCM (fmt=1) must be 8 or 16 bit resolution, got %d instead\n",
				nbits);
			fclose(fp);
			return;
		}
	}
	else if(afmt == 3)
	{
		//TODO: support fp64?
		if(nbits != 32)
		{
			LogError(
				"Floating point PCM (fmt=3) must be 32 bit resolution, got %d instead\n",
				nbits);
			fclose(fp);
			return;
		}
	}
	else
	{
		LogError(
			"Importing compressed WAVs (format %d) is not supported. "
			"Try re-encoding as uncompressed integer or floating point PCM\n",
			afmt);
		fclose(fp);
		return;
	}

	//Read and discard chunks until we see the data header
	while(true)
	{
		if(2 != fread(header, sizeof(uint32_t), 2, fp))
		{
			LogError("Failed to read chunk header\n");
			fclose(fp);
			return;
		}
		if(header[0] == 0x61746164)	//"data"
			break;
		fseek(fp, header[1], SEEK_CUR);
	}

	//Extract some metadata
	size_t datalen = header[1];
	size_t bytes_per_sample = nbits / 8;
	size_t bytes_per_row = bytes_per_sample * nchans;
	size_t nsamples = datalen / bytes_per_row;
	int64_t interval = FS_PER_SECOND / srate;

	//Configure output streams
	SetupStreams(nchans);

	vector<UniformAnalogWaveform*> wfms;
	for(size_t i=0; i<nchans; i++)
	{
		//Create new waveform for channel
		auto wfm = new UniformAnalogWaveform;
		wfm->m_timescale = interval;
		wfm->m_startTimestamp = timestamp;
		wfm->m_startFemtoseconds = fs;
		wfm->m_triggerPhase = 0;
		wfm->Resize(nsamples);
		wfm->PrepareForCpuAccess();
		wfms.push_back(wfm);
		SetData(wfm, i);
	}

	//Read the entire file into a buffer rather than doing a whole bunch of tiny fread's
	uint8_t* buf = new uint8_t[datalen];
	if(datalen != fread(buf, 1, datalen, fp))
	{
		LogError("Failed to read WAV data\n");
		fclose(fp);
		return;
	}
	fclose(fp);
	fp = NULL;

	//TODO: vectorized shuffling for the common case of 2 channel?

	//Crunch the samples
	size_t off = 0;
	for(size_t i=0; i<nsamples; i++)
	{
		for(size_t j=0; j<nchans; j++)
		{
			auto wfm = wfms[j];

			//Floating point samples can be read as is
			if(afmt == 3)
			{
				//Do not violate strict aliasing, compiler will optimize out the memcpy
				float val;
				memcpy(&val,buf+off,sizeof(float));
				wfm->m_samples[i] = val;
				off += 4;
			}

			//Integer samples get normalized
			else
			{
				//16 bit is signed
				if(nbits == 16)
				{
					//Do not violate strict aliasing, compiler will optimize out the memcpy
					int16_t val;
					memcpy(&val,buf+off,sizeof(int16_t));
					wfm->m_samples[i] = val / 32768.0f;
					off += 2;
				}

				//8 bit is unsigned
				else
				{
					wfm->m_samples[i] = (*(uint8_t*)(buf + off) - 127) / 127.0f;
					off ++;
				}
			}
		}
	}

	for(auto w : wfms)
		w->MarkModifiedFromCpu();

	//Done, clean up
	delete[] buf;
}

void WAVImportFilter::SetupStreams(size_t chans)
{
	ClearStreams();

	for(size_t i=0; i<chans; i++)
		AddStream(Unit(Unit::UNIT_VOLTS), string("CH") + to_string(i+1), Stream::STREAM_TYPE_ANALOG);

	//Resize port arrays
	size_t oldsize = m_ranges.size();
	m_ranges.resize(chans);
	m_offsets.resize(chans);

	//If growing, fill new cells with reasonable default values
	for(size_t i=oldsize; i<chans; i++)
	{
		m_ranges[i] = 2;
		m_offsets[i] = 0;
	}

	m_outputsChangedSignal.emit();
}
