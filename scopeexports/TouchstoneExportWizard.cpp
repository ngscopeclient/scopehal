/***********************************************************************************************************************
*                                                                                                                      *
* libscopeexports                                                                                                    *
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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Implementation of TouchstoneExportWizard
 */
#include "scopehal.h"
#include "TouchstoneExportWizard.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TouchstoneExportConfigurationPage

TouchstoneExportConfigurationPage::TouchstoneExportConfigurationPage()
{
	m_grid.attach(m_freqUnitLabel, 0, 0, 1, 1);
		m_freqUnitLabel.set_text("Frequency unit");
		m_freqUnitLabel.set_halign(Gtk::ALIGN_START);
		m_freqUnitLabel.set_margin_right(20);
	m_grid.attach_next_to(m_freqUnitBox, m_freqUnitLabel, Gtk::POS_RIGHT, 1, 1);
		m_freqUnitBox.append("Hz");
		m_freqUnitBox.append("kHz");
		m_freqUnitBox.append("MHz");
		m_freqUnitBox.append("GHz");
		m_freqUnitBox.set_active_text("GHz");
	m_grid.attach_next_to(m_sFormatLabel, m_freqUnitLabel, Gtk::POS_BOTTOM, 1, 1);
		m_sFormatLabel.set_text("Format");
	m_grid.attach_next_to(m_sFormatBox, m_sFormatLabel, Gtk::POS_RIGHT, 1, 1);
		m_sFormatBox.append("MA: Magnitude / Angle");
		m_sFormatBox.append("DB: Magnitude (dB) / Angle");
		m_sFormatBox.append("RI: Real / Imaginary");
		m_sFormatBox.set_active_text("MA: Magnitude / Angle");
	m_grid.attach_next_to(m_portCountLabel, m_sFormatLabel, Gtk::POS_BOTTOM, 1, 1);
		m_portCountLabel.set_text("Port Count");
	m_grid.attach_next_to(m_portCountSpin, m_portCountLabel, Gtk::POS_RIGHT, 1, 1);
		m_portCountSpin.set_digits(0);
		m_portCountSpin.set_increments(1,1);
		m_portCountSpin.set_range(01, 30);
		m_portCountSpin.set_value(2);

	m_grid.show_all();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TouchstoneExportChannelGroup

TouchstoneExportChannelGroup::TouchstoneExportChannelGroup(
	int to,
	int from,
	const vector<OscilloscopeChannel*>& channels)
{
	//Frame and header
	auto paramname = string("S") + to_string(to+1) + to_string(from+1);
	m_frame.set_label(paramname);
	m_frame.add(m_grid);
	m_frame.set_margin_right(20);
	m_frame.set_margin_bottom(20);
	m_frame.get_label_widget()->override_font(Pango::FontDescription("sans bold 14"));

	//Grid for main widget area
	m_grid.set_margin_left(10);
	m_grid.set_margin_right(10);
	m_grid.set_margin_top(10);
	m_grid.set_margin_bottom(10);

	//Main widgets
	m_grid.attach(m_magLabel, 0, 0, 1, 1);
		m_magLabel.set_label("Magnitude");
		m_magLabel.set_margin_right(20);
	m_grid.attach_next_to(m_magBox, m_magLabel, Gtk::POS_RIGHT, 1, 1);
	m_grid.attach_next_to(m_angLabel, m_magLabel, Gtk::POS_BOTTOM, 1, 1);
		m_angLabel.set_label("Angle");
		m_angLabel.set_margin_right(20);
	m_grid.attach_next_to(m_angBox, m_angLabel, Gtk::POS_RIGHT, 1, 1);

	m_magStreams.clear();
	m_angStreams.clear();

	//Populate channel list
	for(auto c : channels)
	{
		//Not analog? Skip it
		if(c->GetType() != OscilloscopeChannel::CHANNEL_TYPE_ANALOG)
			continue;

		for(size_t i=0; i<c->GetStreamCount(); i++)
		{
			StreamDescriptor stream(c, i);

			//X axis should be frequency
			if(stream.GetXAxisUnits() != Unit(Unit::UNIT_HZ))
				continue;

			//Y axis dB is good for magnitude
			if(stream.GetYAxisUnits() == Unit(Unit::UNIT_DB))
			{
				auto sname = stream.GetName();
				m_magBox.append(sname);
				m_magStreams.push_back(stream);
				if(sname.find(paramname) != string::npos)
					m_magBox.set_active_text(sname);
			}

			//Y axis degrees is good for angle
			if(stream.GetYAxisUnits() == Unit(Unit::UNIT_DEGREES))
			{
				auto sname = stream.GetName();
				m_angBox.append(sname);
				m_angStreams.push_back(stream);
				if(sname.find(paramname) != string::npos)
					m_angBox.set_active_text(sname);
			}
		}
	}

	m_frame.show_all();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TouchstoneExportChannelSelectionPage

TouchstoneExportChannelSelectionPage::TouchstoneExportChannelSelectionPage()
{
}

TouchstoneExportChannelSelectionPage::~TouchstoneExportChannelSelectionPage()
{
	Clear();
}

void TouchstoneExportChannelSelectionPage::Clear()
{
	//Delete everything in the grid
	auto children = m_grid.get_children();
	for(auto it : m_groups)
	{
		m_grid.remove(it.second->m_frame);
		delete it.second;
	}
	m_groups.clear();
}

void TouchstoneExportChannelSelectionPage::Refresh(int channelCount, const vector<OscilloscopeChannel*>& channels)
{
	for(int to=0; to<channelCount; to++)
	{
		for(int from=0; from<channelCount; from++)
		{
			auto group = new TouchstoneExportChannelGroup(to, from, channels);
			m_groups[pair<int, int>(to+1, from+1)] = group;

			m_grid.attach(group->m_frame, to, from, 1, 1);
		}
	}

	m_grid.show_all();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TouchstoneExportSummaryPage

TouchstoneExportSummaryPage::TouchstoneExportSummaryPage()
	: m_chooser(Gtk::FILE_CHOOSER_ACTION_SAVE)
{
	m_grid.attach(m_chooser, 0, 0, 1, 1);
}

TouchstoneExportSummaryPage::~TouchstoneExportSummaryPage()
{
}

void TouchstoneExportSummaryPage::Refresh(int channelCount)
{
	auto filters = m_chooser.list_filters();
	for(auto f : filters)
		m_chooser.remove_filter(f);

	auto filter = Gtk::FileFilter::create();
	auto pattern = string("*.s") + to_string(channelCount) + "p";
	filter->add_pattern(pattern);
	filter->set_name(string("Touchstone S-parameter (") + pattern + ")");
	m_chooser.add_filter(filter);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

TouchstoneExportWizard::TouchstoneExportWizard(const vector<OscilloscopeChannel*>& channels)
	: ExportWizard(channels)
{
	//Initial defaults are valid, so mark page as ready to go immediately
	append_page(m_configPage.m_grid);
	set_page_type(m_configPage.m_grid, Gtk::ASSISTANT_PAGE_INTRO);
	set_page_title(m_configPage.m_grid, "Touchstone Format");
	set_page_complete(m_configPage.m_grid);

	append_page(m_channelSelectionPage.m_grid);
	set_page_type(m_channelSelectionPage.m_grid, Gtk::ASSISTANT_PAGE_CONTENT);
	set_page_title(m_channelSelectionPage.m_grid, "Channel Mapping");
	set_page_complete(m_channelSelectionPage.m_grid);

	append_page(m_filePathPage.m_grid);
	set_page_type(m_filePathPage.m_grid, Gtk::ASSISTANT_PAGE_CONFIRM);
	set_page_title(m_filePathPage.m_grid, "File Path");
	set_page_complete(m_filePathPage.m_grid);

	show_all();
}

TouchstoneExportWizard::~TouchstoneExportWizard()
{
}

string TouchstoneExportWizard::GetExportName()
{
	return "Touchstone";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Event handlers

void TouchstoneExportWizard::on_prepare(Gtk::Widget* page)
{
	if(page == &m_channelSelectionPage.m_grid)
		m_channelSelectionPage.Refresh(m_configPage.m_portCountSpin.get_value_as_int(), m_channels);
}

void TouchstoneExportWizard::on_apply()
{
	int nports = m_configPage.m_portCountSpin.get_value_as_int();

	//Set up the output s-params
	SParameters params;
	params.Allocate(nports);

	//Convert from display oriented dB/degrees to linear magnitude / radians (internal SParameters class format).
	//This then gets converted to whatever we need in the actual Touchstone file.
	//For now, assume all inputs have the same frequency spacing etc.
	//TODO: detect this and print error or (ideally) resample
	for(int to=1; to <= nports; to++)
	{
		for(int from=1; from <= nports; from++)
		{
			auto group = m_channelSelectionPage.m_groups[pair<int, int>(to, from)];

			auto magrow = group->m_magBox.get_active_row_number();
			auto angrow = group->m_angBox.get_active_row_number();
			auto magData = dynamic_cast<AnalogWaveform*>(group->m_magStreams[magrow].GetData());
			auto angData = dynamic_cast<AnalogWaveform*>(group->m_angStreams[angrow].GetData());
			if(!magData || !angData)
			{
				LogError("Missing mag or angle data\n");
				continue;
			}

			params[SPair(to, from)].ConvertFromWaveforms(magData, angData);
		}
	}

	//Figure out export timebase unit
	SParameters::FreqUnit freqUnit;
	auto freqUnitText = m_configPage.m_freqUnitBox.get_active_text();
	if(freqUnitText == "Hz")
		freqUnit = SParameters::FREQ_HZ;
	else if(freqUnitText == "kHz")
		freqUnit = SParameters::FREQ_KHZ;
	else if(freqUnitText == "MHz")
		freqUnit = SParameters::FREQ_MHZ;
	else /* if(freqUnitText == "GHz")*/
		freqUnit = SParameters::FREQ_GHZ;

	//Figure out export number format
	SParameters::ParameterFormat format;
	auto sFormatText = m_configPage.m_sFormatBox.get_active_text();
	if(sFormatText.find("MA") == 0)
		format = SParameters::FORMAT_MAG_ANGLE;
	else if(sFormatText.find("DB") == 0)
		format = SParameters::FORMAT_DBMAG_ANGLE;
	else /*if(sFormatText.find("RI") == 0)*/
		format = SParameters::FORMAT_REAL_IMAGINARY;

	//Finally, save it
	params.SaveToFile(
		m_filePathPage.m_chooser.get_filename(),
		format,
		freqUnit);

	hide();
}
