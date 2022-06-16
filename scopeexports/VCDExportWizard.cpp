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
	@brief Implementation of VCDExportWizard
 */
#include "scopehal.h"
#include "VCDExportWizard.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//VCDExportChannelSelectionPage

VCDExportChannelSelectionPage::VCDExportChannelSelectionPage(const vector<OscilloscopeChannel*>& channels)
	: m_selectedChannels(1)
	, m_availableChannels(1)
{
	m_grid.attach(m_selectedFrame, 0, 0, 1, 1);
		m_selectedFrame.set_label("Selected Channels");
		m_selectedFrame.set_margin_start(5);
		m_selectedFrame.set_margin_end(5);
		m_selectedFrame.add(m_selectedChannels);
		m_selectedChannels.set_headers_visible(false);

	m_grid.attach(m_availableFrame, 1, 0, 1, 1);
		m_availableFrame.set_label("Available Channels");
		m_availableFrame.add(m_availableChannels);
		m_availableChannels.set_headers_visible(false);

	m_grid.attach(m_removeButton, 0, 2, 1, 1);
		m_removeButton.set_label(">");
		m_removeButton.set_margin_start(5);
		m_removeButton.set_margin_end(5);
	m_grid.attach(m_addButton, 1, 2, 1, 1);
		m_addButton.set_label("<");

	m_grid.show_all();

	m_addButton.signal_clicked().connect(sigc::mem_fun(*this, &VCDExportChannelSelectionPage::OnAddChannel));
	m_removeButton.signal_clicked().connect(sigc::mem_fun(*this, &VCDExportChannelSelectionPage::OnRemoveChannel));

	for(auto c : channels)
	{
		//Can't export anything but digital data
		if(c->GetType() != OscilloscopeChannel::CHANNEL_TYPE_DIGITAL)
			continue;

		//Must be a time domain waveform, nothing else makes sense for VCD
		if(c->GetXAxisUnits() != Unit(Unit::UNIT_FS))
			continue;

		//Check each stream
		for(size_t s=0; s<c->GetStreamCount(); s++)
		{
			StreamDescriptor stream(c, s);

			//Must actually have data
			if(stream.GetData() == nullptr)
				continue;

			//Save in list of targets
			auto name = stream.GetName();
			m_availableChannels.append(name);
			m_targets[name] = stream;
		}
	}
}

void VCDExportChannelSelectionPage::OnAddChannel()
{
	//See what row we selected
	auto sel = m_availableChannels.get_selected();
	if(sel.empty())
		return;
	auto index = sel[0];
	auto name = m_availableChannels.get_text(index);

	//Add to the current channels list
	m_selectedChannels.append(name);

	//But also remove from the available channel list
	auto store = Glib::RefPtr<Gtk::ListStore>::cast_dynamic(m_availableChannels.get_model());
	auto selpath = m_availableChannels.get_selection()->get_selected_rows()[0];
	store->erase(store->get_iter(selpath));
}

void VCDExportChannelSelectionPage::OnRemoveChannel()
{
	//See what row we selected
	auto sel = m_selectedChannels.get_selected();
	if(sel.empty())
		return;
	auto index = sel[0];
	auto name = m_selectedChannels.get_text(index);

	//Add to the available channels list
	m_availableChannels.append(name);

	//But also remove from the selected channel list
	auto store = Glib::RefPtr<Gtk::ListStore>::cast_dynamic(m_selectedChannels.get_model());
	auto selpath = m_selectedChannels.get_selection()->get_selected_rows()[0];
	store->erase(store->get_iter(selpath));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// VCDExportFinalPage

VCDExportFinalPage::VCDExportFinalPage()
	: m_chooser(Gtk::FILE_CHOOSER_ACTION_SAVE)
{
	auto filter = Gtk::FileFilter::create();
	filter->add_pattern("*.vcd");
	filter->set_name("Value Change Dump (*.vcd)");
	m_chooser.add_filter(filter);

	m_grid.attach(m_chooser, 0, 0, 1, 1);

	m_grid.show_all();
}

VCDExportFinalPage::~VCDExportFinalPage()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

VCDExportWizard::VCDExportWizard(const vector<OscilloscopeChannel*>& channels)
	: ExportWizard(channels)
	, m_channelSelectionPage(channels)
{
	append_page(m_channelSelectionPage.m_grid);
	set_page_type(m_channelSelectionPage.m_grid, Gtk::ASSISTANT_PAGE_INTRO);
	set_page_title(m_channelSelectionPage.m_grid, "Select Channels");

	//can move on immediately, no requirement to select a channel
	set_page_complete(m_channelSelectionPage.m_grid);

	append_page(m_finalPage.m_grid);
	set_page_type(m_finalPage.m_grid, Gtk::ASSISTANT_PAGE_CONFIRM);
	set_page_title(m_finalPage.m_grid, "File Path");
	set_page_complete(m_finalPage.m_grid);

	show_all();
}

VCDExportWizard::~VCDExportWizard()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Page sequencing

void VCDExportWizard::on_prepare(Gtk::Widget* /*page*/)
{
}

void VCDExportWizard::on_apply()
{
	//Get output streams
	vector<StreamDescriptor> streams;
	size_t len = m_channelSelectionPage.m_selectedChannels.size();
	for(size_t i=0; i<len; i++)
	{
		auto name = m_channelSelectionPage.m_selectedChannels.get_text(i);
		streams.push_back(m_channelSelectionPage.m_targets[name]);
	}
	Unit fs(Unit::UNIT_FS);

	//Get waveforms for each stream
	vector<DigitalWaveform*> waveforms;
	vector<size_t> indexes;
	vector<size_t> lens;
	for(auto s : streams)
	{
		auto wfm = dynamic_cast<DigitalWaveform*>(s.GetData());	//chooser does not allow us to select anything else
		waveforms.push_back(wfm);
		indexes.push_back(0);
		lens.push_back(wfm->m_offsets.size());
	}

	//Write header section
	auto fname = m_finalPage.m_chooser.get_filename();
	FILE* fp = fopen(fname.c_str(), "w");
	if(!fp)
	{
		LogError("Failed to open output file\n");
		return;
	}

	auto tnow = time(nullptr);
	auto local = localtime(&tnow);
	char timebuf[128] = {0};
	strftime(timebuf, sizeof(timebuf), "%F %T", local);
	fprintf(fp, "$date\n");
	fprintf(fp, "    %s\n", timebuf);
	fprintf(fp, "$end\n");
	fprintf(fp, "$version\n");
	fprintf(fp, "    glscopeclient (build date %s %s)\n", __DATE__, __TIME__);	//TODO: add git sha etc
	fprintf(fp, "$end\n");
	fprintf(fp, "$timescale 1fs\n");

	//Dump the list of variables (for now, all a single module)
	std::map<size_t, string> ids;
	fprintf(fp, "$scope module export $end\n");
	for(size_t i=0; i<streams.size(); i++)
	{
		string id = "";
		size_t j = i;
		while(true)
		{
			//Prepend the new ID digit (base 52)
			size_t digit = j % 52;
			char c;
			if(digit < 26)
				c = 'a' + digit;
			else
				c = 'A' + digit - 26;
			id = string(1, c) + id;

			//Move on
			j /= 52;
			if(j == 0)
				break;
		}
		ids[i] = id;

		//Convert string to be fully alphanumeric
		string name = streams[i].GetName();
		for(size_t k=0; k<name.length(); k++)
		{
			if(!isalnum(name[k]))
				name[k] = '_';
		}

		//TODO: support digital vectors
		fprintf(fp, "    $var wire 1 %3s %s $end\n",
			id.c_str(),
			name.c_str());
	}
	fprintf(fp, "$upscope $end\n");
	fprintf(fp, "$enddefinitions $end\n");
	fprintf(fp, "$dumpvars\n");

	//Print the actual waveform
	//TODO: more efficient, don't export every signal if only one has changed
	int64_t timestamp = 0;
	while(true)
	{
		//Print signal values
		fprintf(fp, "#%ld\n", timestamp);
		for(size_t i=0; i<streams.size(); i++)
			fprintf(fp,"%d%s\n", (bool)waveforms[i]->m_samples[indexes[i]], ids[i].c_str());

		//Get timestamp of next event on any channel
		int64_t next = timestamp;
		for(size_t i=0; i<streams.size(); i++)
		{
			int64_t t = Filter::GetNextEventTimestampScaled(waveforms[i], indexes[i], lens[i], timestamp);
			if(i == 0)
				next = t;
			else
				next = min(next, t);
		}

		//If we can't move forward, stop
		if(next == timestamp)
			break;

		//Move on
		timestamp = next;
		for(size_t i=0; i<streams.size(); i++)
			Filter::AdvanceToTimestampScaled(waveforms[i], indexes[i], lens[i], timestamp);
	}

	fclose(fp);
	hide();
}

string VCDExportWizard::GetExportName()
{
	return "VCD";
}
