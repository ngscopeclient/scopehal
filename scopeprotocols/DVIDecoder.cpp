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
#include "DVIDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

VideoScanlinePacket::~VideoScanlinePacket()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DVIDecoder::DVIDecoder(const string& color)
	: PacketDecoder(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_SERIAL)
{
	//Set up channels
	CreateInput("D0 (blue)");
	CreateInput("D1 (green)");
	CreateInput("D2 (red)");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DVIDecoder::NeedsConfig()
{
	//channels have to be selected
	return true;
}

bool DVIDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i <= 2) && (dynamic_cast<TMDSDecoder*>(stream.m_channel) != NULL ) )
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
		GetInputDisplayName(0).c_str(),
		GetInputDisplayName(1).c_str(),
		GetInputDisplayName(2).c_str()
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

bool DVIDecoder::GetShowImageColumn()
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DVIDecoder::Refresh()
{
	ClearPackets();

	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto dblue = dynamic_cast<TMDSWaveform*>(GetInputWaveform(0));
	auto dgreen = dynamic_cast<TMDSWaveform*>(GetInputWaveform(1));
	auto dred = dynamic_cast<TMDSWaveform*>(GetInputWaveform(2));

	//Create the capture
	DVIWaveform* cap = new DVIWaveform;
	cap->m_timescale = 1;
	cap->m_startTimestamp = dblue->m_startTimestamp;
	cap->m_startFemtoseconds = dblue->m_startFemtoseconds;

	size_t iblue = 0;
	size_t igreen = 0;
	size_t ired = 0;

	TMDSSymbol::TMDSType last_type = TMDSSymbol::TMDS_TYPE_ERROR;

	VideoScanlinePacket* current_packet = NULL;
	int current_pixels = 0;

	//Decode the actual data
	size_t bsize = dblue->m_offsets.size();
	size_t wgsize = dgreen->m_offsets.size();
	size_t rsize = dred->m_offsets.size();
	for(iblue = 0; (iblue < bsize) && (igreen < wgsize) && (ired < rsize); )
	{
		auto sblue = dblue->m_samples[iblue];

		//Control code in master channel? Decode it
		if(sblue.m_type == TMDSSymbol::TMDS_TYPE_CONTROL)
		{
			//If the last sample was data, save the packet for the scanline or data island
			if( (last_type == TMDSSymbol::TMDS_TYPE_DATA) && (current_packet != NULL) )
			{
				current_packet->m_len = dblue->m_offsets[iblue] + dblue->m_durations[iblue] - current_packet->m_offset;
				char tmp[32];
				snprintf(tmp, sizeof(tmp), "%d", current_pixels);
				current_packet->m_headers["Width"] = tmp;
				m_packets.push_back(current_packet);

				current_pixels = 0;
				current_packet = NULL;
			}

			//Extract synchronization signals from blue channel
			//Red/green have status signals that aren't used in DVI.
			bool hsync = (sblue.m_data & 1) ? true : false;
			bool vsync = (sblue.m_data & 2) ? true : false;

			//If this symbol matches the previous one, just extend it
			//rather than creating a new symbol
			size_t last = cap->m_durations.size()-1;
			if( (cap->m_samples.size() > 0) && (dblue->m_samples[iblue-1] == sblue) )
				cap->m_durations[last] = dblue->m_offsets[iblue] + dblue->m_durations[iblue] - cap->m_offsets[last];

			else if(vsync)
			{
				auto pack = new Packet;
				pack->m_offset = dblue->m_offsets[iblue];
				pack->m_headers["Type"] = "VSYNC";
				m_packets.push_back(pack);

				cap->m_offsets.push_back(dblue->m_offsets[iblue]);
				cap->m_durations.push_back(dblue->m_durations[iblue]);
				cap->m_samples.push_back(DVISymbol(DVISymbol::DVI_TYPE_VSYNC));
			}

			else if(hsync)
			{
				cap->m_offsets.push_back(dblue->m_offsets[iblue]);
				cap->m_durations.push_back(dblue->m_durations[iblue]);
				cap->m_samples.push_back(DVISymbol(DVISymbol::DVI_TYPE_HSYNC));
			}

			else
			{
				cap->m_offsets.push_back(dblue->m_offsets[iblue]);
				cap->m_durations.push_back(dblue->m_durations[iblue]);
				cap->m_samples.push_back(DVISymbol(DVISymbol::DVI_TYPE_PREAMBLE));
			}
		}

		//Data? Decode it
		else if(sblue.m_type == TMDSSymbol::TMDS_TYPE_DATA)
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
					if(ngreen >= wgsize)
						continue;
					if(dgreen->m_samples[ngreen-1].m_type != TMDSSymbol::TMDS_TYPE_CONTROL)
						continue;
					if(dgreen->m_samples[ngreen].m_type != TMDSSymbol::TMDS_TYPE_DATA)
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
					if(nred >= rsize)
						continue;
					if(dred->m_samples[nred-1].m_type != TMDSSymbol::TMDS_TYPE_CONTROL)
						continue;
					if(dred->m_samples[nred].m_type != TMDSSymbol::TMDS_TYPE_DATA)
						continue;

					ired += delta;
					break;
				}

				//Start a new packet
				current_packet = new VideoScanlinePacket;
				current_packet->m_offset = dblue->m_offsets[iblue];
				current_packet->m_headers["Type"] = "Video";
				current_pixels = 0;
			}

			auto sgreen = dgreen->m_samples[igreen];
			auto sred = dred->m_samples[ired];

			cap->m_offsets.push_back(dblue->m_offsets[iblue]);
			cap->m_durations.push_back(dblue->m_durations[iblue]);
			cap->m_samples.push_back(DVISymbol(DVISymbol::DVI_TYPE_VIDEO,
				sred.m_data,
				sgreen.m_data,
				sblue.m_data));

			//In-memory packet data is RGB order for compatibility with Gdk::Pixbuf
			//may be null if waveform starts halfway through a scan line. Don't make a packet for that.
			if(current_packet != NULL)
			{
				current_packet->m_data.push_back(sred.m_data);
				current_packet->m_data.push_back(sgreen.m_data);
				current_packet->m_data.push_back(sblue.m_data);
				current_pixels ++;
			}
		}

		//Save the previous type of sample
		last_type = sblue.m_type;

		//Default to incrementing all channels
		iblue ++;
		igreen ++;
		ired ++;
	}

	if(current_packet)
		delete current_packet;

	SetData(cap, 0);
}

Gdk::Color DVIDecoder::GetColor(int i)
{
	DVIWaveform* capture = dynamic_cast<DVIWaveform*>(GetData(0));
	if(capture != NULL)
	{
		auto s = capture->m_samples[i];
		switch(s.m_type)
		{
			case DVISymbol::DVI_TYPE_PREAMBLE:
				return m_standardColors[COLOR_PREAMBLE];

			case DVISymbol::DVI_TYPE_HSYNC:
			case DVISymbol::DVI_TYPE_VSYNC:
				return m_standardColors[COLOR_CONTROL];

			case DVISymbol::DVI_TYPE_VIDEO:
				{
					Gdk::Color ret;
					ret.set_rgb_p(s.m_red / 255.0f, s.m_green / 255.0f, s.m_blue / 255.0f);
					return ret;
				}

			case DVISymbol::DVI_TYPE_ERROR:
			default:
				return m_standardColors[COLOR_ERROR];
		}
	}

	//error
	return m_standardColors[COLOR_ERROR];
}

string DVIDecoder::GetText(int i)
{
	DVIWaveform* capture = dynamic_cast<DVIWaveform*>(GetData(0));
	if(capture != NULL)
	{
		auto s = capture->m_samples[i];
		char tmp[32];
		switch(s.m_type)
		{
			case DVISymbol::DVI_TYPE_PREAMBLE:
				return "BLANK";

			case DVISymbol::DVI_TYPE_HSYNC:
				return "HSYNC";

			case DVISymbol::DVI_TYPE_VSYNC:
				return "VSYNC";

			case DVISymbol::DVI_TYPE_VIDEO:
				snprintf(tmp, sizeof(tmp), "#%02x%02x%02x", s.m_red, s.m_green, s.m_blue);
				break;

			case DVISymbol::DVI_TYPE_ERROR:
			default:
				return "ERROR";

		}
		return string(tmp);
	}
	return "";
}
