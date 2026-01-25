/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Implementation of DSIFrameDecoder
 */

#include "../scopehal/scopehal.h"
#include "DSIPacketDecoder.h"
#include "DSIFrameDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DSIFrameDecoder::DSIFrameDecoder(const string& color)
	: PacketDecoder(color, CAT_SERIAL)
{
	AddProtocolStream("data");
	CreateInput("DSI");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool DSIFrameDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == nullptr)
		return false;

	if( (i == 0) && (dynamic_cast<DSIPacketDecoder*>(stream.m_channel) != nullptr ) )
		return true;

	return false;
}

string DSIFrameDecoder::GetProtocolName()
{
	return "MIPI DSI Frame";
}

vector<string> DSIFrameDecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("Width");
	ret.push_back("Checksum");
	return ret;
}

bool DSIFrameDecoder::GetShowImageColumn()
{
	return true;
}

Filter::DataLocation DSIFrameDecoder::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DSIFrameDecoder::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("DSIFrameDecoder::Refresh");
	#endif

	ClearPackets();
	ClearErrors();

	if(!VerifyAllInputsOK())
	{
		if(!GetInput(0))
			AddErrorMessage("Missing inputs", "No signal input connected");
		else if(!GetInputWaveform(0))
			AddErrorMessage("Missing inputs", "No waveform available at input");

		SetData(nullptr, 0);
		return;
	}
	auto din = dynamic_cast<DSIWaveform*>(GetInputWaveform(0));
	din->PrepareForCpuAccess();

	//Create the capture
	auto cap = SetupEmptyWaveform<DSIFrameWaveform>(din, 0);
	cap->PrepareForCpuAccess();

	enum
	{
		STATE_IDLE,
		STATE_ID,
		STATE_EXTEND_DATA,

		STATE_RGB888_START,
		STATE_RGB888_RED,
		STATE_RGB888_GREEN,
		STATE_RGB888_BLUE
	} state = STATE_IDLE;

	VideoScanlinePacket* pack = nullptr;

	//Decode the actual data
	size_t len = din->m_offsets.size();
	int64_t tstart = 0;
	uint8_t red = 0;
	uint8_t green = 0;
	uint8_t blue = 0;
	for(size_t i=0; i<len; i++)
	{
		auto s = din->m_samples[i];
		auto off = din->m_offsets[i];
		auto end = off + din->m_durations[i];

		size_t nout = cap->m_offsets.size();
		size_t last = nout - 1;

		switch(state)
		{
			//When we get a VC ID, that's the start of a new packet.
			case STATE_IDLE:
				break;

			//Look at the packet type and figure out what to do with it
			case STATE_ID:
				if(s.m_stype == DSISymbol::TYPE_IDENTIFIER)
				{
					//Ignore the rest of the packet by default
					state = STATE_IDLE;

					switch(s.m_data)
					{
						case DSIPacketDecoder::TYPE_VSYNC_START:
							cap->m_offsets.push_back(tstart);
							cap->m_durations.push_back(end - tstart);
							cap->m_samples.push_back(DSIFrameSymbol(DSIFrameSymbol::TYPE_VSYNC));
							break;

						case DSIPacketDecoder::TYPE_HSYNC_START:
							cap->m_offsets.push_back(tstart);
							cap->m_durations.push_back(end - tstart);
							cap->m_samples.push_back(DSIFrameSymbol(DSIFrameSymbol::TYPE_HSYNC));
							break;

						//Extend H/V sync packets
						case DSIPacketDecoder::TYPE_HSYNC_END:
							if( (nout > 0) && (cap->m_samples[last].m_type == DSIFrameSymbol::TYPE_HSYNC) )
								state = STATE_EXTEND_DATA;
							break;
						case DSIPacketDecoder::TYPE_VSYNC_END:
							if( (nout > 0) && (cap->m_samples[last].m_type == DSIFrameSymbol::TYPE_VSYNC) )
								state = STATE_EXTEND_DATA;
							break;

						//Ignore most other types

						//RGB is start of a video frame
						//TODO: support other video formats
						case DSIPacketDecoder::TYPE_PACKED_PIXEL_RGB888:
							state = STATE_RGB888_START;
							break;

						default:
							break;
					}
				}
				else
					state = STATE_IDLE;
				break;

			//Extend the current sample to the end of this packet header
			case STATE_EXTEND_DATA:
				if(s.m_stype == DSISymbol::TYPE_DATA)
				{}
				else if( (s.m_stype == DSISymbol::TYPE_ECC_OK) || (s.m_stype == DSISymbol::TYPE_ECC_BAD) )
				{
					cap->m_durations[last] = end - cap->m_offsets[last];
					state = STATE_IDLE;
				}
				else
					state = STATE_IDLE;
				break;

			//Decode video
			case STATE_RGB888_START:
				//Create packet
				pack = new VideoScanlinePacket;
				pack->m_offset = off * cap->m_timescale;
				pack->m_headers["Checksum"] = "Not checked";

			//fall through
			case STATE_RGB888_RED:
				if(s.m_stype == DSISymbol::TYPE_DATA)
				{
					tstart = off;
					red = s.m_data;
					state = STATE_RGB888_GREEN;
				}
				else if(s.m_stype == DSISymbol::TYPE_CHECKSUM_OK)
					pack->m_headers["Checksum"] = "OK";
				else if(s.m_stype == DSISymbol::TYPE_CHECKSUM_BAD)
					pack->m_headers["Checksum"] = "Error";

				break;

			case STATE_RGB888_GREEN:
				if(s.m_stype == DSISymbol::TYPE_DATA)
				{
					green = s.m_data;
					state = STATE_RGB888_BLUE;
				}
				break;

			case STATE_RGB888_BLUE:
				if(s.m_stype == DSISymbol::TYPE_DATA)
				{
					blue = s.m_data;

					pack->m_data.push_back(red);
					pack->m_data.push_back(green);
					pack->m_data.push_back(blue);
					pack->m_len = (end * cap->m_timescale) - pack->m_offset;

					pack->m_headers["Width"] = to_string(pack->m_data.size() / 3);

					cap->m_offsets.push_back(tstart);
					cap->m_durations.push_back(end - tstart);
					cap->m_samples.push_back(DSIFrameSymbol(
						DSIFrameSymbol::TYPE_VIDEO, red, green, blue));

					state = STATE_RGB888_RED;
				}
				break;
		}

		//Always start a new packet when we see a VC ID
		if(s.m_stype == DSISymbol::TYPE_VC)
		{
			tstart = off;
			state = STATE_ID;

			//End the current packet
			if(pack)
			{
				if(pack->m_data.size() != 0)
					m_packets.push_back(pack);
				else
					delete pack;
				pack = nullptr;
			}
		}
	}

	//End the current packet
	if(pack)
	{
		if(pack->m_data.size() != 0)
			m_packets.push_back(pack);
		else
			delete pack;
		pack = nullptr;
	}

	cap->MarkModifiedFromCpu();
}

string DSIFrameWaveform::GetColor(size_t i)
{
	auto s = m_samples[i];
	switch(s.m_type)
	{
		case DSIFrameSymbol::TYPE_HSYNC:
		case DSIFrameSymbol::TYPE_VSYNC:
			return StandardColors::colors[StandardColors::COLOR_CONTROL];

		case DSIFrameSymbol::TYPE_VIDEO:
			{
				char buf[10];
				snprintf(buf, sizeof(buf), "#%02X%02X%02X", s.m_red, s.m_green, s.m_blue);
				return buf;
			}

		case DSIFrameSymbol::TYPE_ERROR:
		default:
			return StandardColors::colors[StandardColors::COLOR_ERROR];
	}
}

string DSIFrameWaveform::GetText(size_t i)
{
	auto s = m_samples[i];
	char tmp[32];
	switch(s.m_type)
	{
		case DSIFrameSymbol::TYPE_HSYNC:
			return "HSYNC";

		case DSIFrameSymbol::TYPE_VSYNC:
			return "VSYNC";

		case DSIFrameSymbol::TYPE_VIDEO:
			snprintf(tmp, sizeof(tmp), "#%02x%02x%02x", s.m_red, s.m_green, s.m_blue);
			break;

		case DSIFrameSymbol::TYPE_ERROR:
		default:
			return "ERROR";

	}
	return string(tmp);
}
