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
#include "TouchstoneImportFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

TouchstoneImportFilter::TouchstoneImportFilter(const string& color)
	: SParameterSourceFilter(color, CAT_GENERATION)
	, m_fpname("Touchstone File")
{
	m_parameters[m_fpname] = FilterParameter(FilterParameter::TYPE_FILENAME, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_fpname].m_fileFilterMask = "*.s*p";
	m_parameters[m_fpname].m_fileFilterName = "Touchstone S-parameter files (*.s*p)";
	m_parameters[m_fpname].signal_changed().connect(sigc::mem_fun(*this, &TouchstoneImportFilter::OnFileNameChanged));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool TouchstoneImportFilter::ValidateChannel(size_t /*i*/, StreamDescriptor /*stream*/)
{
	//no inputs
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string TouchstoneImportFilter::GetProtocolName()
{
	return "Touchstone Import";
}

void TouchstoneImportFilter::SetDefaultName()
{
	auto fname = m_parameters[m_fpname].ToString();

	char hwname[256];
	snprintf(hwname, sizeof(hwname), "%s", BaseName(fname).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void TouchstoneImportFilter::OnFileNameChanged()
{
	auto fname = m_parameters[m_fpname].ToString();
	if(fname.empty())
		return;

	//Load the touchstone file
	TouchstoneParser parser;
	if(!parser.Load(fname, m_params))
		return;
	time_t timestamp = 0;
	int64_t fs = 0;
	GetTimestampOfFile(fname, timestamp, fs);
	size_t nports = m_params.GetNumPorts();

	//Recreate our output streams
	SetupStreams();

	//Run the actual import
	for(size_t to=0; to < nports; to++)
	{
		for(size_t from=0; from < nports; from++)
		{
			//Extract our parameters
			auto& vec = m_params[SPair(to+1, from+1)];
			size_t nsamples = vec.size();

			size_t base = (to*nports + from) * 2;

			//Create new waveform for magnitude and phase channels
			auto mwfm = new AnalogWaveform;
			mwfm->m_timescale = 1;
			mwfm->m_startTimestamp = timestamp;
			mwfm->m_startFemtoseconds = fs;
			mwfm->m_triggerPhase = 0;
			mwfm->m_densePacked = false;	//don't assume uniform frequency spacing
			mwfm->Resize(nsamples);
			SetData(mwfm, base);

			auto pwfm = new AnalogWaveform;
			pwfm->m_timescale = 1;
			pwfm->m_startTimestamp = timestamp;
			pwfm->m_startFemtoseconds = fs;
			pwfm->m_triggerPhase = 0;
			pwfm->m_densePacked = false;	//don't assume uniform frequency spacing
			pwfm->Resize(nsamples);
			SetData(pwfm, base + 1);

			//Populate them
			float angscale = 180 / M_PI;	//we use degrees for display
			for(size_t i=0; i<nsamples; i++)
			{
				auto& point = vec[i];

				//Convert magnitude to dB
				mwfm->m_offsets[i] = point.m_frequency;
				mwfm->m_durations[i] = 1;
				mwfm->m_samples[i] = 20 * log10(point.m_amplitude);

				//Convert phase to degrees
				pwfm->m_offsets[i] = point.m_frequency;
				pwfm->m_durations[i] = 1;
				pwfm->m_samples[i] = point.m_phase * angscale;

				//Update previous sample's duration if possible
				if(i > 0)
				{
					mwfm->m_durations[i-1] = mwfm->m_offsets[i] - mwfm->m_offsets[i-1];
					pwfm->m_durations[i-1] = pwfm->m_offsets[i] - pwfm->m_offsets[i-1];
				}
			}
		}
	}
}

void TouchstoneImportFilter::Refresh()
{
	//everything happens in OnFileNameChanged
}
