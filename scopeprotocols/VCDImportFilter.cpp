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
#include "VCDImportFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

VCDImportFilter::VCDImportFilter(const string& color)
	: ImportFilter(color)
{
	m_fpname = "VCD File";
	m_parameters[m_fpname] = FilterParameter(FilterParameter::TYPE_FILENAME, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_fpname].m_fileFilterMask = "*.vcd";
	m_parameters[m_fpname].m_fileFilterName = "Value Change Dump files (*.vcd)";
	m_parameters[m_fpname].signal_changed().connect(sigc::mem_fun(*this, &VCDImportFilter::OnFileNameChanged));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string VCDImportFilter::GetProtocolName()
{
	return "VCD Import";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void VCDImportFilter::OnFileNameChanged()
{
	auto fname = m_parameters[m_fpname].ToString();
	if(fname.empty())
		return;

	//Set waveform timestamp to file timestamp
	time_t timestamp = 0;
	int64_t fs = 0;
	GetTimestampOfFile(fname, timestamp, fs);

	FILE* fp = fopen(fname.c_str(), "r");
	if(!fp)
	{
		LogError("Couldn't open VCD file \"%s\"\n", fname.c_str());
		return;
	}

	ClearStreams();

	enum
	{
		STATE_IDLE,
		STATE_DATE,
		STATE_VERSION,
		STATE_TIMESCALE,
		STATE_VARS,
		STATE_INITIAL,
		STATE_DUMP
	} state = STATE_IDLE;

	int64_t timescale = 1;

	int64_t current_time = 0;

	//Current scope prefix for signals
	vector<string> scope;

	//Map of signal IDs to signals
	map<string, WaveformBase*> waveforms;
	map<string, size_t> widths;

	//VCD is a line based format, so process everything in lines
	char buf[2048];
	while(NULL != fgets(buf, sizeof(buf), fp))
	{
		string s = Trim(buf);

		//Changing time is always legal, even before we get to the main variable dumping section.
		//(Xilinx Vivado-generated VCDs include a #0 before the $dumpvars section.)
		if(s[0] == '#')
		{
			current_time = stoll(buf+1);
			continue;
		}

		//Scope is a bit special since it can nest. Handle that separately.
		else if(s.find("$scope") != string::npos)
		{
			//Get the actual scope
			char name[128];
			if(1 == sscanf(buf, "$scope module %127s", name))
				scope.push_back(name);
			state = STATE_VARS;
			continue;
		}

		//Main state machine
		switch(state)
		{
			case STATE_IDLE:
				if(s == "$date")
					state = STATE_DATE;
				else if(s == "$version")
					state = STATE_VERSION;
				else if(s == "$timescale")
					state = STATE_TIMESCALE;
				else if(s == "$dumpvars")
					state = STATE_INITIAL;
				else
					LogWarning("Don't know what to do with line %s\n", s.c_str());
				break;	//end STATE_IDLE

			case STATE_DATE:
				if(s[0] != '$')
				{
					tm now;
					time_t tnow;
					time(&tnow);
					localtime_r(&tnow, &now);

					tm stamp;

					//Read the date
					//Assume it's formatted "Fri May 21 07:16:38 2021" for now
					char dow[16];
					char month[16];
					if(7 == sscanf(
						buf,
						"%3s %3s %d %d:%d:%d %d",
						dow, month, &stamp.tm_mday, &stamp.tm_hour, &stamp.tm_min, &stamp.tm_sec, &stamp.tm_year))
					{
						string sm(month);
						if(sm == "Jan")
							stamp.tm_mon = 0;
						else if(sm == "Feb")
							stamp.tm_mon = 1;
						else if(sm == "Mar")
							stamp.tm_mon = 2;
						else if(sm == "Apr")
							stamp.tm_mon = 3;
						else if(sm == "May")
							stamp.tm_mon = 4;
						else if(sm == "Jun")
							stamp.tm_mon = 5;
						else if(sm == "Jul")
							stamp.tm_mon = 6;
						else if(sm == "Aug")
							stamp.tm_mon = 7;
						else if(sm == "Sep")
							stamp.tm_mon = 8;
						else if(sm == "Oct")
							stamp.tm_mon = 9;
						else if(sm == "Nov")
							stamp.tm_mon = 10;
						else
							stamp.tm_mon = 11;

						//tm_year isn't absolute year, it's offset from 1900
						stamp.tm_year -= 1900;

						//TODO: figure out if this day/month/year was DST or not.
						//For now, assume same as current. This is going to be off by an hour for half the year!
						stamp.tm_isdst = now.tm_isdst;

						//We can finally get the actual time_t
						timestamp = mktime(&stamp);
					}
				}
				break;	//end STATE_DATE;

			case STATE_VERSION:
				//ignore
				break;	//end STATE_VERSION

			case STATE_TIMESCALE:
				if(s[0] != '$')
				{
					Unit ufs(Unit::UNIT_FS);
					timescale = ufs.ParseString(s);
				}
				break;	//end STATE_VERSION

			case STATE_VARS:
				if(s.find("$upscope") != string::npos)
				{
					if(!scope.empty())
						scope.pop_back();
				}
				else if(s.find("$enddefinitions") != string::npos)
					state = STATE_IDLE;
				else
				{
					//Format the current scope
					string sscope;
					for(auto level : scope)
						sscope += level + "/";

					//Parse the line
					char vtype[16];	//"reg" or "wire", ignored
					int width;
					char symbol[16];
					char name[128];
					if(4 != sscanf(buf, " $var %15[^ ] %d %15[^ ] %127[^ ]", vtype, &width, symbol, name))
						continue;

					//If the symbol is already in use, skip it.
					//We don't support one symbol with more than one name for now
					if(waveforms.find(symbol) != waveforms.end())
						continue;

					//Create the stream
					AddDigitalStream(sscope + name);

					//Create the waveform
					WaveformBase* wfm;
					if(width == 1)
						wfm = new DigitalWaveform;
					else
						wfm = new DigitalBusWaveform;

					wfm->m_timescale = timescale;
					wfm->m_startTimestamp = timestamp;
					wfm->m_startFemtoseconds = fs;
					wfm->m_triggerPhase = 0;
					wfm->m_densePacked = false;
					waveforms[symbol] = wfm;
					widths[symbol] = width;
					SetData(wfm, m_streams.size() - 1);
				}
				break;	//end STATE_VARS

			case STATE_INITIAL:
			case STATE_DUMP:

				//Parse the current line
				if(s[0] != '$')
				{
					//Vector: first char is 'b', then data, space, symbol name
					if(s[0] == 'b')
					{
						auto ispace = s.find(' ');
						auto symbol = s.substr(ispace + 1);
						auto wfm = dynamic_cast<DigitalBusWaveform*>(waveforms[symbol]);
						if(wfm)
						{
							//Parse the sample data (skipping the leading 'b')
							vector<bool> sample;
							for(size_t i = ispace-1; i > 0; i--)
							{
								if(s[i] == '1')
									sample.push_back(true);
								else
									sample.push_back(false);
							}

							//Zero-pad the sample out to full width
							auto width = widths[symbol];
							while(sample.size() < width)
								sample.push_back(false);

							//Extend the previous sample, if there is one
							auto len = wfm->m_samples.size();
							if(len)
							{
								auto last = len-1;
								wfm->m_durations[last] = current_time - wfm->m_offsets[last];
							}

							//Add the new sample
							wfm->m_offsets.push_back(current_time);
							wfm->m_durations.push_back(1);
							wfm->m_samples.push_back(sample);
						}
						else
							LogError("Symbol \"%s\" is not a valid digital bus waveform\n", symbol.c_str());
					}

					//Scalar: first char is boolean value, rest is symbol name
					else
					{
						auto symbol = s.substr(1);
						auto wfm = dynamic_cast<DigitalWaveform*>(waveforms[symbol]);
						if(wfm)
						{
							//Extend the previous sample, if there is one
							auto len = wfm->m_samples.size();
							if(len)
							{
								auto last = len-1;
								wfm->m_durations[last] = current_time - wfm->m_offsets[last];
							}

							//Add the new sample
							wfm->m_offsets.push_back(current_time);
							wfm->m_durations.push_back(1);
							wfm->m_samples.push_back(s[0] == '1');
						}
						else
							LogError("Symbol \"%s\" is not a valid digital waveform\n", symbol.c_str());
					}
				}

				break;	//end STATE_INITIAL / STATE_DUMP
		}

		//Reset at the end of a block
		if(s.find("$end") != string::npos)
		{
			if(state == STATE_INITIAL)
				state = STATE_DUMP;
			else if(state != STATE_VARS)
				state = STATE_IDLE;
		}
	}
	fclose(fp);

	//Nothing to do if we didn't get any channels
	if(m_streams.empty())
		return;

	//Find the longest common prefix from all signal names
	auto prefix = m_streams[0].m_name;
	for(size_t i=1; i<m_streams.size(); i++)
	{
		auto name = m_streams[i].m_name;
		size_t nlen = 1;
		for(; (nlen < prefix.length()) && (nlen < name.length()); nlen ++)
		{
			if(name[nlen] != prefix[nlen])
				break;
		}
		prefix.resize(nlen);
	}

	//Remove the prefix from all signal names
	for(size_t i=0; i<m_streams.size(); i++)
		m_streams[i].m_name = m_streams[i].m_name.substr(prefix.length());

	m_outputsChangedSignal.emit();
}
