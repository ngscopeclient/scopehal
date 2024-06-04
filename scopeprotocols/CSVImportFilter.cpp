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
#include "CSVImportFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

CSVImportFilter::CSVImportFilter(const string& color)
	: ImportFilter(color)
	, m_xunit("X Axis Unit")
	, m_yunit0("Y Axis Unit 0")
{
	m_fpname = "CSV File";
	m_parameters[m_fpname] = FilterParameter(FilterParameter::TYPE_FILENAME, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_fpname].m_fileFilterMask = "*.csv";
	m_parameters[m_fpname].m_fileFilterName = "Comma Separated Value files (*.csv)";
	m_parameters[m_fpname].signal_changed().connect(sigc::mem_fun(*this, &CSVImportFilter::OnFileNameChanged));

	m_parameters[m_xunit] = FilterParameter::UnitSelector();
	m_parameters[m_xunit].SetIntVal(Unit::UNIT_FS);
	m_parameters[m_xunit].signal_changed().connect(sigc::mem_fun(*this, &CSVImportFilter::OnFileNameChanged));

	m_parameters[m_yunit0] = FilterParameter::UnitSelector();;
	m_parameters[m_yunit0].SetIntVal(Unit::UNIT_VOLTS);
	m_parameters[m_yunit0].signal_changed().connect(sigc::mem_fun(*this, &CSVImportFilter::OnFileNameChanged));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string CSVImportFilter::GetProtocolName()
{
	return "CSV Import";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void CSVImportFilter::OnFileNameChanged()
{
	auto fname = m_parameters[m_fpname].ToString();
	if(fname.empty())
		return;

	//Set unit
	SetXAxisUnits(Unit(static_cast<Unit::UnitType>(m_parameters[m_xunit].GetIntVal())));

	//Set waveform timestamp to file timestamp
	time_t timestamp = 0;
	int64_t fs = 0;
	GetTimestampOfFile(fname, timestamp, fs);

	FILE* fp = fopen(fname.c_str(), "r");
	if(!fp)
	{
		LogError("Couldn't open CSV file \"%s\"\n", fname.c_str());
		return;
	}

	ClearStreams();

	//Read the file
	vector<string> names;
	vector< vector<string> > lines;
	vector<int64_t> timestamps;
	char line[1024];
	bool digilentFormat;
	size_t nrow = 0;
	size_t ncols = 0;
	while(!feof(fp))
	{
		if(!fgets(line, sizeof(line), fp))
			break;

		nrow ++;

		//Discard blank lines
		string s = Trim(line);
		if(s.empty())
			continue;

		//If the line starts with a #, it's a comment. Discard it, but save timestamp metadata if present
		if(s[0] == '#')
		{
			if(s == "#Digilent WaveForms Oscilloscope Acquisition")
				digilentFormat = true;

			else if(digilentFormat)
			{
				if(s.find("#Date Time: ") == 0)
				{
					//yyyy-mm-dd hh:mm:ss.ms.us.ns
					//No time zone information provided. For now, assume current time zone.
					string stimestamp = s.substr(12);

					tm now;
					time_t tnow;
					time(&tnow);
					localtime_r(&tnow, &now);

					tm stamp;
					int ms;
					int us;
					int ns;
					if(9 == sscanf(stimestamp.c_str(), "%d-%d-%d %d:%d:%d.%d.%d.%d",
						&stamp.tm_year, &stamp.tm_mon, &stamp.tm_mday,
						&stamp.tm_hour, &stamp.tm_min, &stamp.tm_sec,
						&ms, &us, &ns))
					{
						//tm_year isn't absolute year, it's offset from 1900
						stamp.tm_year -= 1900;

						//TODO: figure out if this day/month/year was DST or not.
						//For now, assume same as current. This is going to be off by an hour for half the year!
						stamp.tm_isdst = now.tm_isdst;

						//We can finally get the actual time_t
						timestamp = mktime(&stamp);

						//Convert to femtoseconds for internal scopehal format
						fs = ms * 1000;
						fs = (fs + us) * 1000;
						fs = (fs + ns) * 1000;
						fs *= 1000;
					}
				}
			}
			continue;
		}

		//Parse into 2D vector of timestamps and strings
		string tmp;
		bool foundTimestamp = false;
		vector<string> fields;
		bool headerRow = false;
		for(size_t i=0; i<s.length(); i++)
		{
			//End of field
			if( (s[i] == ',') || (s[i] == '\n') )
			{
				//If this is the first row, check if it's numeric
				if(names.empty() && timestamps.empty())
				{
					//See if it's a header row
					for(size_t j=0; (j<sizeof(line)) && (line[j] != '\0'); j++)
					{
						auto c = line[j];
						if(	!isdigit(c) && !isspace(c) &&
							(c != ',') && (c != '.') && (c != '-') && (c != 'e'))
						{
							headerRow = true;
							break;
						}
					}

					//Save the header values
					fields.push_back(tmp);
				}

				//Load timestamp
				else if(!foundTimestamp)
				{
					foundTimestamp = true;

					//Parse time to a float and convert to fs
					if(m_parameters[m_xunit].GetIntVal() == Unit::UNIT_FS)
					{
						double timeSec;
						if(tmp.find("e") == string::npos)
							sscanf(tmp.c_str(), "%lf", &timeSec);
						else
							sscanf(tmp.c_str(), "%le", &timeSec);
						timestamps.push_back(FS_PER_SECOND * timeSec);
					}

					//other units are as-is
					else
						timestamps.push_back(stoll(tmp));
				}

				//Data field. Save it
				else
					fields.push_back(tmp);
				tmp = "";
			}

			//Add to field
			else
				tmp += s[i];
		}
		if(tmp != "")
			fields.push_back(tmp);

		//Header row gets special treatment
		if(headerRow)
		{
			//delete name of timestamp column
			fields.erase(fields.begin());

			names = fields;
			continue;
		}

		//Sanity check field count
		if(ncols == 0)
			ncols = fields.size();
		else if(ncols != fields.size())
		{
			LogError("Malformed file (line %zu contains %zu fields, but file started with %zu fields)\n",
				nrow, ncols, fields.size()
				);
			break;
		}

		lines.push_back(fields);
	}

	//Assign default names to channels if there's no header row
	if(names.empty())
	{
		char tmp[32];
		for(size_t i=0; i<ncols; i++)
		{
			snprintf(tmp, sizeof(tmp), "Field%zu", i);
			names.push_back(tmp);
		}
	}

	//Figure out if channels are analog or digital and create output streams/waveforms
	vector<SparseDigitalWaveform*> digwaves;
	vector<SparseAnalogWaveform*> anwaves;
	for(size_t i=0; i<ncols; i++)
	{
		LogIndenter li;

		//Assume digital, then change to analog if we see anything other than a 0/1 in the first 10 lines
		bool digital = true;
		for(size_t j=0; j<lines.size() && j<10; j++)
		{
			string field = lines[j][i];
			if( (field != "0") && (field != "1") )
			{
				digital = false;
				break;
			}
		}

		//Create the output stream
		if(digital)
		{
			AddStream(Unit(Unit::UNIT_COUNTS), names[i], Stream::STREAM_TYPE_DIGITAL);

			auto wfm = new SparseDigitalWaveform;
			wfm->m_timescale = 1;
			wfm->m_startTimestamp = timestamp;
			wfm->m_startFemtoseconds = fs;
			wfm->m_triggerPhase = 0;
			wfm->Resize(lines.size());
			digwaves.push_back(wfm);

			//no analog waveform
			anwaves.push_back(NULL);
			SetData(wfm, i);
		}
		else
		{
			//TODO: support arbitrarily many y axis unit fields, for now use unit 0 for everything
			AddStream(
				Unit(static_cast<Unit::UnitType>(m_parameters[m_yunit0].GetIntVal())),
				names[i],
				Stream::STREAM_TYPE_ANALOG);

			auto wfm = new SparseAnalogWaveform;
			wfm->m_timescale = 1;
			wfm->m_startTimestamp = timestamp;
			wfm->m_startFemtoseconds = fs;
			wfm->m_triggerPhase = 0;
			wfm->Resize(lines.size());
			anwaves.push_back(wfm);

			//no digital waveform
			digwaves.push_back(NULL);
			SetData(wfm, i);
		}
	}

	m_outputsChangedSignal.emit();

	//Process each actual waveform and figure out how to handle it
	for(size_t i=0; i<ncols; i++)
	{
		if(digwaves[i])
		{
			auto wfm = digwaves[i];

			//Read the sample data
			for(size_t j=0; j<lines.size(); j++)
			{
				wfm->m_offsets[j] = timestamps[j];

				//Last one? copy previous sample duration
				if(j+1 == lines.size())
					wfm->m_durations[j] = wfm->m_durations[j-1];

				//Set sample duration of previous sample
				if(j > 0)
					wfm->m_durations[j-1] = wfm->m_offsets[j] - wfm->m_offsets[j-1];

				//Read waveform data
				if(lines[j][i] == "1")
					wfm->m_samples[j] = true;
				else
					wfm->m_samples[j] = false;
			}

			if(TryNormalizeTimebase(wfm))
			{
				auto dense = new UniformDigitalWaveform(*wfm);
				dense->MarkModifiedFromCpu();
				SetData(dense, i);
			}
			else
			{
				wfm->MarkModifiedFromCpu();

				//If we end up with zero length samples due to invalid configuration, nuke the channel
				if(wfm->empty() || (wfm->m_durations[0] == 0) )
					SetData(nullptr, i);
			}
		}

		//Analog data
		else
		{
			auto wfm = anwaves[i];

			//Read the sample data
			for(size_t j=0; j<lines.size(); j++)
			{
				wfm->m_offsets[j] = timestamps[j];

				//Last one? copy previous sample duration
				if(j+1 == lines.size())
					wfm->m_durations[j] = wfm->m_durations[j-1];

				//Set sample duration of previous sample
				if(j > 0)
					wfm->m_durations[j-1] = wfm->m_offsets[j] - wfm->m_offsets[j-1];

				//Read waveform data
				float v;
				auto tmp = lines[j][i];
				if(tmp.find("e") == string::npos)
					sscanf(tmp.c_str(), "%f", &v);
				else
					sscanf(tmp.c_str(), "%e", &v);
				wfm->m_samples[j] = v;
			}

			if(TryNormalizeTimebase(wfm))
			{
				auto dense = new UniformAnalogWaveform(*wfm);
				dense->MarkModifiedFromCpu();
				SetData(dense, i);
			}
			else
			{
				wfm->MarkModifiedFromCpu();

				//If we end up with zero length samples due to invalid configuration, nuke the channel
				if(wfm->m_durations[0] == 0)
					SetData(nullptr, i);
			}
		}
	}
}
