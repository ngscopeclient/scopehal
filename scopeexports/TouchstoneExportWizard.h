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
	@brief Declaration of TouchstoneExportWizard
 */

#ifndef TouchstoneExportWizard_h
#define TouchstoneExportWizard_h

/**
	@brief Initial configuration
 */
class TouchstoneExportConfigurationPage
{
public:
	TouchstoneExportConfigurationPage();

	Gtk::Grid m_grid;
		Gtk::Label m_freqUnitLabel;
			Gtk::ComboBoxText m_freqUnitBox;
		Gtk::Label m_sFormatLabel;
			Gtk::ComboBoxText m_sFormatBox;
		Gtk::Label m_portCountLabel;
			Gtk::SpinButton m_portCountSpin;
};

class TouchstoneExportChannelGroup
{
public:
	TouchstoneExportChannelGroup(int to, int from, const std::vector<OscilloscopeChannel*>& channels);

	Gtk::Frame m_frame;
		Gtk::Grid m_grid;
			Gtk::Label m_magLabel;
				Gtk::ComboBoxText m_magBox;
			Gtk::Label m_angLabel;
				Gtk::ComboBoxText m_angBox;

	std::vector<StreamDescriptor> m_magStreams;
	std::vector<StreamDescriptor> m_angStreams;
};

/**
	@brief Select channels to export to Touchstone
 */
class TouchstoneExportChannelSelectionPage
{
public:
	TouchstoneExportChannelSelectionPage();
	virtual ~TouchstoneExportChannelSelectionPage();

	void Clear();
	void Refresh(int channelCount, const std::vector<OscilloscopeChannel*>& channels);

	Gtk::Grid m_grid;
		Gtk::Label m_timestampTypeLabel;
		Gtk::ComboBoxText m_timestampTypeBox;

	std::map<std::pair<int, int>, TouchstoneExportChannelGroup*> m_groups;
};

/**
	@brief Select channels to export to Touchstone
 */
class TouchstoneExportSummaryPage
{
public:
	TouchstoneExportSummaryPage();
	virtual ~TouchstoneExportSummaryPage();

	void Refresh(int channelCount);

	Gtk::Grid m_grid;
		Gtk::FileChooserWidget m_chooser;
};

/**
	@brief Touchstone exporter
 */
class TouchstoneExportWizard : public ExportWizard
{
public:
	TouchstoneExportWizard(const std::vector<OscilloscopeChannel*>& channels);
	virtual ~TouchstoneExportWizard();

	static std::string GetExportName();

	EXPORT_WIZARD_INITPROC(TouchstoneExportWizard)

protected:

	virtual void on_prepare(Gtk::Widget* page);
	virtual void on_apply();

	TouchstoneExportConfigurationPage m_configPage;
	TouchstoneExportChannelSelectionPage m_channelSelectionPage;
	//TODO: error check page to detect if something is inconsistent
	TouchstoneExportSummaryPage m_filePathPage;
};

#endif
