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
	@brief Implementation of CSVExportWizard
 */
#include "scopehal.h"
#include "CSVExportWizard.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CSVExportChannelSelectionPage

CSVExportReferenceChannelSelectionPage::CSVExportReferenceChannelSelectionPage(const std::vector<OscilloscopeChannel*>& channels)
{
	m_grid.attach(m_captionLabel, 0, 0, 2, 1);
		m_captionLabel.set_label(
			"Select the timebase reference channel.\n"
			"\n"
			"This is the leftmost data column in the generated CSV, and its X axis sample interval maps\n"
			"to the row interval for the exported data. On the next page, you will only be able to add\n"
			"channels with the same X axis unit as this channel.\n"
			"\n"
			"Eye patterns, spectrograms, and other 2D datasets cannot be exported to CSV."
			"\n"
			);
	m_grid.attach(m_referenceLabel, 0, 1, 1, 1);
		m_referenceLabel.set_label("Reference Channel");
	m_grid.attach(m_referenceBox, 1, 1, 1, 1);

	//Populate the reference box with a list of all channels that are legal to use
	for(auto c : channels)
	{
		//Check each stream
		for(size_t s=0; s<c->GetStreamCount(); s++)
		{
			StreamDescriptor stream(c, s);

			//Can't export 2D density plots
			auto type = stream.GetType();
			if( (type == Stream::STREAM_TYPE_EYE) ||
				(type == Stream::STREAM_TYPE_SPECTROGRAM) )
			{
				continue;
			}


			//Must actually have data
			if(stream.GetData() == NULL)
				continue;

			m_referenceBox.append(stream.GetName());
			m_streams.push_back(stream);
		}
	}
	m_referenceBox.set_active(0);

	m_grid.show_all();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//CSVExportOtherChannelSelectionPage

CSVExportOtherChannelSelectionPage::CSVExportOtherChannelSelectionPage(
	const CSVExportReferenceChannelSelectionPage& ref)
	: m_selectedChannels(1)
	, m_availableChannels(1)
	, m_ref(ref)
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

	m_addButton.signal_clicked().connect(sigc::mem_fun(*this, &CSVExportOtherChannelSelectionPage::OnAddChannel));
	m_removeButton.signal_clicked().connect(sigc::mem_fun(*this, &CSVExportOtherChannelSelectionPage::OnRemoveChannel));
}

void CSVExportOtherChannelSelectionPage::UpdateChannelList()
{
	m_availableChannels.clear_items();
	m_selectedChannels.clear_items();
	m_targets.clear();

	//Make a list of compatible streams
	auto& streams = m_ref.GetStreams();
	auto refStream = m_ref.GetActiveChannel();
	for(auto s : streams)
	{
		//Reference channel can't be exported again in another column
		if(s == refStream)
			continue;

		//Can't export 2D density plots
		auto type = s.GetType();
		if( (type == Stream::STREAM_TYPE_EYE) ||
			(type == Stream::STREAM_TYPE_SPECTROGRAM) )
		{
			continue;
		}

		//Must be non-null
		auto chan = s.m_channel;
		if(!chan || !s.GetData())
			continue;

		//Must have same X axis unit as reference
		if(chan->GetXAxisUnits() != refStream.GetXAxisUnits())
			continue;

		//Save in list of targets
		auto name = s.GetName();
		m_availableChannels.append(name);
		m_targets[name] = s;
	}
}

void CSVExportOtherChannelSelectionPage::OnAddChannel()
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

void CSVExportOtherChannelSelectionPage::OnRemoveChannel()
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
// CSVExportFinalPage

CSVExportFinalPage::CSVExportFinalPage()
	: m_chooser(Gtk::FILE_CHOOSER_ACTION_SAVE)
{
	auto filter = Gtk::FileFilter::create();
	filter->add_pattern("*.csv");
	filter->set_name("Comma Separated Value (*.csv)");
	m_chooser.add_filter(filter);

	m_grid.attach(m_chooser, 0, 0, 1, 1);

	m_grid.show_all();
}

CSVExportFinalPage::~CSVExportFinalPage()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

CSVExportWizard::CSVExportWizard(const vector<OscilloscopeChannel*>& channels)
	: ExportWizard(channels)
	, m_referenceSelectionPage(channels)
	, m_otherChannelSelectionPage(m_referenceSelectionPage)
{
	append_page(m_referenceSelectionPage.m_grid);
	set_page_type(m_referenceSelectionPage.m_grid, Gtk::ASSISTANT_PAGE_INTRO);
	set_page_title(m_referenceSelectionPage.m_grid, "Select Timebase Reference Channel");

	//a channel is always selected, so we can move on immediately
	set_page_complete(m_referenceSelectionPage.m_grid);

	append_page(m_otherChannelSelectionPage.m_grid);
	set_page_type(m_otherChannelSelectionPage.m_grid, Gtk::ASSISTANT_PAGE_CONTENT);
	set_page_title(m_otherChannelSelectionPage.m_grid, "Select Other Channels");

	//can move on immediately, no requirement to select a channel
	set_page_complete(m_otherChannelSelectionPage.m_grid);

	append_page(m_finalPage.m_grid);
	set_page_type(m_finalPage.m_grid, Gtk::ASSISTANT_PAGE_CONFIRM);
	set_page_title(m_finalPage.m_grid, "File Path");
	set_page_complete(m_finalPage.m_grid);

	show_all();
}

CSVExportWizard::~CSVExportWizard()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Page sequencing

void CSVExportWizard::on_prepare(Gtk::Widget* page)
{
	if(page == &m_otherChannelSelectionPage.m_grid)
		m_otherChannelSelectionPage.UpdateChannelList();
}

void CSVExportWizard::on_apply()
{
	//Timebase reference channel
	vector<StreamDescriptor> streams;
	streams.push_back(m_referenceSelectionPage.GetActiveChannel());
	auto timebaseUnit = streams[0].GetXAxisUnits();

	//Other channels
	size_t len = m_otherChannelSelectionPage.m_selectedChannels.size();
	for(size_t i=0; i<len; i++)
	{
		auto name = m_otherChannelSelectionPage.m_selectedChannels.get_text(i);
		streams.push_back(m_otherChannelSelectionPage.m_targets[name]);
	}

	//Write header row
	auto fname = m_finalPage.m_chooser.get_filename();
	FILE* fp = fopen(fname.c_str(), "w");
	if(!fp)
	{
		LogError("Failed to open output file\n");
		return;
	}
	if(timebaseUnit == Unit(Unit::UNIT_FS))
		fprintf(fp, "Time (s)");
	else if(timebaseUnit == Unit(Unit::UNIT_HZ))
		fprintf(fp, "Frequency (Hz)");
	else
		fprintf(fp, "X Unit");
	for(auto s : streams)
		fprintf(fp, ",%s", s.GetName().c_str());
	fprintf(fp, "\n");

	//Prepare to generate output waveform
	vector<WaveformBase*> waveforms;
	vector<size_t> indexes;
	for(auto s : streams)
	{
		waveforms.push_back(s.GetData());
		indexes.push_back(0);
	}
	auto timebaseWaveform = waveforms[0];

	//Write data
	//TODO: lots of redundant casting, this can probably be optimized!
	int64_t lastTimestamp = INT64_MIN;
	auto timebaseSparse = dynamic_cast<SparseWaveformBase*>(timebaseWaveform);
	auto timebaseUniform = dynamic_cast<UniformWaveformBase*>(timebaseWaveform);
	auto timebaseSparseAnalog = dynamic_cast<SparseAnalogWaveform*>(timebaseWaveform);
	auto timebaseUniformAnalog = dynamic_cast<UniformAnalogWaveform*>(timebaseWaveform);
	auto timebaseSparseDigital = dynamic_cast<SparseDigitalWaveform*>(timebaseWaveform);
	auto timebaseUniformDigital = dynamic_cast<UniformDigitalWaveform*>(timebaseWaveform);
	for(size_t i=0; i<timebaseWaveform->size(); i++)
	{
		//Get current timestamp
		auto timestamp = GetOffsetScaled(timebaseSparse, timebaseUniform, i);

		//Write timestamp
		if(timebaseUnit == Unit(Unit::UNIT_FS))
			fprintf(fp, "%e", timestamp / FS_PER_SECOND);
		else if(timebaseUnit == Unit(Unit::UNIT_HZ))
			fprintf(fp, "%ld", timestamp);
		else
			fprintf(fp, "%ld", timestamp);

		//Write data from the reference channel as-is (no interpolation, it's the timebase by definition)
		auto reftype = streams[0].GetType();
		switch(reftype)
		{
			case Stream::STREAM_TYPE_ANALOG:
				fprintf(fp, ",%f", GetValue(timebaseSparseAnalog, timebaseUniformAnalog, i));
				break;

			case Stream::STREAM_TYPE_DIGITAL:
				fprintf(fp, ",%d", GetValue(timebaseSparseDigital, timebaseUniformDigital, i));
				break;

			case Stream::STREAM_TYPE_PROTOCOL:
				fprintf(fp, ",%s", timebaseWaveform->GetText(i).c_str());
				break;

			default:
				break;
		}

		//Write additional channel data
		for(size_t j=1; j<waveforms.size(); j++)
		{
			//Find closest sample
			size_t k = indexes[j];
			auto w = waveforms[j];
			int64_t sstart = 0;
			int64_t send = 0;
			auto sw = dynamic_cast<SparseWaveformBase*>(w);
			auto uw = dynamic_cast<UniformWaveformBase*>(w);
			for(; k < w->size(); k++)
			{
				sstart = GetOffsetScaled(sw, uw, k);
				send = sstart + GetDurationScaled(sw, uw, k);

				//If this sample ends in the future, we're good to go.
				if(send > timestamp)
				{
					indexes[j] = k;
					break;
				}
			}
			k = indexes[j];

			//See if this is the first time we've seen this sample
			//(if our timestamp is within it, but the previous timestamp was not)
			bool firstHit = (timestamp >= sstart) && (lastTimestamp < sstart);

			//Separate processing is needed depending on the data type
			auto type = streams[j].GetType();
			switch(type)
			{
				//Linear interpolation
				case Stream::STREAM_TYPE_ANALOG:
					{
						//No interpolation for last sample since there's no next to lerp to
						auto uan = dynamic_cast<UniformAnalogWaveform*>(w);
						auto san = dynamic_cast<SparseAnalogWaveform*>(w);

						if(k+1 > w->size())
							fprintf(fp, ",%f", GetValue(san, uan, k));

						//Interpolate
						else
						{
							float vleft = GetValue(san, uan, k);
							float vright = GetValue(san, uan, k+1);

							int64_t tleft = sstart;
							int64_t tright = GetDurationScaled(san, uan, k+1);

							float frac = 1.0 * (timestamp - tleft) / (tright - tleft);

							float flerp = vleft + frac * (vright-vleft);
							fprintf(fp, ",%f", flerp);
						}
					}
					break;

				//Nearest neighbor interpolation
				case Stream::STREAM_TYPE_DIGITAL:
					{
						auto udig = dynamic_cast<UniformDigitalWaveform*>(w);
						auto sdig = dynamic_cast<SparseDigitalWaveform*>(w);
						fprintf(fp, ",%d", GetValue(sdig, udig, k));
					}
					break;

				//First-hit "interpolation"
				case Stream::STREAM_TYPE_PROTOCOL:
					{
						if(firstHit)
							fprintf(fp, ",%s", w->GetText(k).c_str());
						else
							fprintf(fp, ",");
					}
					break;

				default:
					break;
			}

		}

		fprintf(fp, "\n");
		lastTimestamp = timestamp;
	}

	fclose(fp);

	hide();
}

string CSVExportWizard::GetExportName()
{
	return "CSV";
}
