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
#include "ExportFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ExportFilter::ExportFilter(const string& color)
	: Filter(color, CAT_EXPORT)
	, m_fname("File name")
	, m_mode("Update mode")
	, m_fp(nullptr)
{
	//No output stream

	//Create an input stream

	//Add a reference to us so that we're never deleted
	//TODO: This is not a good long term solution because it will cause memory leaks!
	//We need some way to allow deletion
	AddRef();

	m_parameters[m_fname] = FilterParameter(FilterParameter::TYPE_FILENAME, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_fname].m_fileIsOutput = true;
	m_parameters[m_fname].signal_changed().connect(sigc::mem_fun(*this, &ExportFilter::OnFileNameChanged));

	m_parameters[m_mode] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_mode].AddEnumValue("Append (continuous)", MODE_CONTINUOUS_APPEND);
	m_parameters[m_mode].AddEnumValue("Append (manual)", MODE_MANUAL_APPEND);
	m_parameters[m_mode].AddEnumValue("Overwrite (continuous)", MODE_CONTINUOUS_OVERWRITE);
	m_parameters[m_mode].AddEnumValue("Overwrite (manual)", MODE_MANUAL_OVERWRITE);

	//Default to manual trigger mode so we don't have the file grow huge before the user can react
	m_parameters[m_mode].SetIntVal(MODE_MANUAL_OVERWRITE);
}

ExportFilter::~ExportFilter()
{
	if(m_fp)
		fclose(m_fp);
	m_fp = nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void ExportFilter::Refresh()
{
	auto mode = static_cast<ExportMode_t>(m_parameters[m_mode].GetIntVal());
	switch(mode)
	{
		case MODE_CONTINUOUS_OVERWRITE:
			Clear();
			//fall through

		case MODE_CONTINUOUS_APPEND:
			Export();

		//Manual mode - don't do anything during Refresh()
		case MODE_MANUAL_APPEND:
		case MODE_MANUAL_OVERWRITE:
			break;
	}
}

vector<string> ExportFilter::EnumActions()
{
	vector<string> ret;
	ret.push_back("Clear");
	ret.push_back("Export");
	return ret;
}

void ExportFilter::PerformAction(const string& id)
{
	if(id == "Clear")
		Clear();
	if(id == "Export")
		Export();
}

/**
	@brief Handle change of file name

	Just close the file if open. We'll re-open (and add a header) next export.
 */
void ExportFilter::OnFileNameChanged()
{
	if(m_fp)
		fclose(m_fp);
	m_fp = nullptr;
}

/**
	@brief Clear the file
 */
void ExportFilter::Clear()
{
	//Close the file if it was open
	if(m_fp)
		fclose(m_fp);
	m_fp = nullptr;

	//Open and truncate it, but do not keep open (so the next Export() treats the file as not open and writes headers)
	FILE* ftmp = fopen(m_parameters[m_fname].GetFileName().c_str(), "wb");
	fclose(ftmp);
}
