/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2020 Andrew D. Zonenberg                                                                          *
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
	@brief Implementation of DVIDecoder
 */

#include "../scopehal/scopehal.h"
#include "TMDSDecoder.h"
#include "../scopehal/ChannelRenderer.h"
#include "../scopehal/TextRenderer.h"
#include "DVIRenderer.h"
#include "DVIDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

VideoScanlinePacket::~VideoScanlinePacket()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DVIDecoder::DVIDecoder(string color)
	: PacketDecoder(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_SERIAL)
{
	//Set up channels
	m_signalNames.push_back("D0 (blue)");
	m_channels.push_back(NULL);

	m_signalNames.push_back("D1 (green)");
	m_channels.push_back(NULL);

	m_signalNames.push_back("D2 (red)");
	m_channels.push_back(NULL);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DVIDecoder::NeedsConfig()
{
	//channels have to be selected
	return true;
}

ChannelRenderer* DVIDecoder::CreateRenderer()
{
	return new DVIRenderer(this);
}

bool DVIDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i <= 2) && (dynamic_cast<TMDSDecoder*>(channel) != NULL ) )
		return true;
	return false;
}

string DVIDecoder::GetProtocolName()
{
	return "DVI";
}

void DVIDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "DVI(%s,%s,%s)",
		m_channels[0]->m_displayname.c_str(),
		m_channels[1]->m_displayname.c_str(),
		m_channels[2]->m_displayname.c_str()
		);
	m_hwname = hwname;
	m_displayname = m_hwname;
}

vector<string> DVIDecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("Type");
	ret.push_back("Width");
	return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DVIDecoder::Refresh()
{
	//Get the input data
	if( (m_channels[0] == NULL) || (m_channels[1] == NULL) || (m_channels[2] == NULL) )
	{
		SetData(NULL);
		return;
	}

	TMDSCapture* dblue = dynamic_cast<TMDSCapture*>(m_channels[0]->GetData());
	TMDSCapture* dgreen = dynamic_cast<TMDSCapture*>(m_channels[1]->GetData());
	TMDSCapture* dred = dynamic_cast<TMDSCapture*>(m_channels[2]->GetData());
	if( (dblue == NULL) || (dgreen == NULL) || (dred == NULL) )
	{
		SetData(NULL);
		return;
	}

	//Create the capture
	DVICapture* cap = new DVICapture;
	cap->m_timescale = 1;
	cap->m_startTimestamp = dblue->m_startTimestamp;
	cap->m_startPicoseconds = dblue->m_startPicoseconds;

	size_t iblue = 0;
	size_t igreen = 0;
	size_t ired = 0;

	TMDSSymbol::TMDSType last_type = TMDSSymbol::TMDS_TYPE_ERROR;

	VideoScanlinePacket* current_packet = NULL;
	int current_pixels = 0;

	//Decode the actual data
	for(iblue = 0; (iblue < dblue->size()) && (igreen < dgreen->size()) && (ired < dred->size()); )
	{
		auto sblue = dblue->m_samples[iblue];

		//Control code in master channel? Decode it
		if(sblue.m_sample.m_type == TMDSSymbol::TMDS_TYPE_CONTROL)
		{
			//If the last sample was data, save the packet for the scanline or data island
			if( (last_type == TMDSSymbol::TMDS_TYPE_DATA) && (current_packet != NULL) )
			{
				current_packet->m_len = sblue.m_offset + sblue.m_duration - current_packet->m_offset;
				char tmp[32];
				snprintf(tmp, sizeof(tmp), "%d", current_pixels);
				current_packet->m_headers["Width"] = tmp;
				m_packets.push_back(current_packet);

				current_pixels = 0;
				current_packet = NULL;
			}

			//Extract synchronization signals from blue channel
			//Red/green have status signals that aren't used in DVI.
			bool hsync = (sblue.m_sample.m_data & 1) ? true : false;
			bool vsync = (sblue.m_sample.m_data & 2) ? true : false;

			//If this symbol matches the previous one, just extend it
			//rather than creating a new symbol
			if( (cap->m_samples.size() > 0) && (dblue->m_samples[iblue-1].m_sample == sblue.m_sample) )
			{
				auto& last_sample = cap->m_samples[cap->m_samples.size()-1];
				last_sample.m_duration = sblue.m_offset + sblue.m_duration - last_sample.m_offset;
			}

			else if(vsync)
			{
				cap->m_samples.push_back(DVISample(
					sblue.m_offset, sblue.m_duration,
					DVISymbol(DVISymbol::DVI_TYPE_VSYNC)));
			}

			else if(hsync)
			{
				cap->m_samples.push_back(DVISample(
					sblue.m_offset, sblue.m_duration,
					DVISymbol(DVISymbol::DVI_TYPE_HSYNC)));
			}

			else
			{
				cap->m_samples.push_back(DVISample(
					sblue.m_offset, sblue.m_duration,
					DVISymbol(DVISymbol::DVI_TYPE_PREAMBLE)));
			}
		}

		//Data? Decode it
		else if(sblue.m_sample.m_type == TMDSSymbol::TMDS_TYPE_DATA)
		{
			//If the LAST sample was a control symbol, re-synchronize the three lanes
			//to compensate for lane-to-lane clock skew.
			//Should only be needed at the start of the capture, but can't hurt to re-do it in case of some
			//weird clock domain crossing issues in the transmitter causing idle insertion/removal.
			if(last_type == TMDSSymbol::TMDS_TYPE_CONTROL)
			{
				//Align green
				for(ssize_t delta=-50; delta <= 50; delta ++)
				{
					size_t ngreen = delta + igreen;
					if(ngreen < 1)
						continue;
					if(ngreen >= dgreen->size())
						continue;
					if(dgreen->m_samples[ngreen-1].m_sample.m_type != TMDSSymbol::TMDS_TYPE_CONTROL)
						continue;
					if(dgreen->m_samples[ngreen].m_sample.m_type != TMDSSymbol::TMDS_TYPE_DATA)
						continue;

					igreen += delta;
					break;
				}

				//Align red
				for(ssize_t delta= -50; delta <= 50; delta ++)
				{
					size_t nred = delta + ired;
					if(nred < 1)
						continue;
					if(nred >= dred->size())
						continue;
					if(dred->m_samples[nred-1].m_sample.m_type != TMDSSymbol::TMDS_TYPE_CONTROL)
						continue;
					if(dred->m_samples[nred].m_sample.m_type != TMDSSymbol::TMDS_TYPE_DATA)
						continue;

					ired += delta;
					break;
				}

				//Start a new packet
				current_packet = new VideoScanlinePacket;
				current_packet->m_offset = sblue.m_offset;
				current_packet->m_headers["Type"] = "Video";
				current_pixels = 0;
			}

			auto sgreen = dgreen->m_samples[igreen];
			auto sred = dred->m_samples[ired];

			cap->m_samples.push_back(DVISample(
				sblue.m_offset, sblue.m_duration,
				DVISymbol(DVISymbol::DVI_TYPE_VIDEO,
				sred.m_sample.m_data,
				sgreen.m_sample.m_data,
				sblue.m_sample.m_data)));

			//may be null if waveform starts halfway through a scan line. Don't make a packet for that.
			if(current_packet != NULL)
			{
				current_packet->m_data.push_back(sred.m_sample.m_data);
				current_packet->m_data.push_back(sgreen.m_sample.m_data);
				current_packet->m_data.push_back(sblue.m_sample.m_data);
				current_pixels ++;
			}
		}

		//Save the previous type of sample
		last_type = sblue.m_sample.m_type;

		//Default to incrementing all channels
		iblue ++;
		igreen ++;
		ired ++;
	}

	if(current_packet)
		delete current_packet;

	SetData(cap);
}
