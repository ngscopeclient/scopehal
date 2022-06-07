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
	@brief Declaration of CSVExportWizard
 */

#ifndef CSVExportWizard_h
#define CSVExportWizard_h

/**
	@brief Select reference channel
 */
class CSVExportReferenceChannelSelectionPage
{
public:
	CSVExportReferenceChannelSelectionPage(const std::vector<OscilloscopeChannel*>& channels);

	Gtk::Grid m_grid;
		Gtk::Label m_captionLabel;
		Gtk::Label m_referenceLabel;
		Gtk::ComboBoxText m_referenceBox;

	StreamDescriptor GetActiveChannel() const
	{ return m_streams[m_referenceBox.get_active_row_number()]; }

	const std::vector<StreamDescriptor>& GetStreams() const
	{ return m_streams; }

protected:
	std::vector<StreamDescriptor> m_streams;
};

/**
	@brief Select other channels
 */
class CSVExportOtherChannelSelectionPage
{
public:
	CSVExportOtherChannelSelectionPage(const CSVExportReferenceChannelSelectionPage& ref);

	Gtk::Grid m_grid;
		Gtk::Frame m_selectedFrame;
			Gtk::ListViewText m_selectedChannels;
		Gtk::Frame m_availableFrame;
			Gtk::ListViewText m_availableChannels;

		Gtk::Button m_removeButton;
		Gtk::Button m_addButton;

	void UpdateChannelList();

	std::map<std::string, StreamDescriptor> m_targets;

protected:
	const CSVExportReferenceChannelSelectionPage& m_ref;

	void OnAddChannel();
	void OnRemoveChannel();
};

/**
	@brief Final configuration and output path
 */
class CSVExportFinalPage
{
public:
	CSVExportFinalPage();
	virtual ~CSVExportFinalPage();

	Gtk::Grid m_grid;
		Gtk::FileChooserWidget m_chooser;

protected:
};

/**
	@brief CSV exporter
 */
class CSVExportWizard : public ExportWizard
{
public:
	CSVExportWizard(const std::vector<OscilloscopeChannel*>& channels);
	virtual ~CSVExportWizard();

	static std::string GetExportName();

	EXPORT_WIZARD_INITPROC(CSVExportWizard)

protected:
	virtual void on_prepare(Gtk::Widget* page);
	virtual void on_apply();

	CSVExportReferenceChannelSelectionPage m_referenceSelectionPage;
	CSVExportOtherChannelSelectionPage m_otherChannelSelectionPage;
	CSVExportFinalPage m_finalPage;
};

#endif
