/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2017 Andrew D. Zonenberg                                                                          *
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
#include "EthernetProtocolDecoder.h"
#include "../scopehal/ChannelRenderer.h"
#include "../scopehal/TextRenderer.h"
#include "EthernetRenderer.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

EthernetProtocolDecoder::EthernetProtocolDecoder(
	string hwname, string color)
	: ProtocolDecoder(hwname, OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color)
{
	//Set up channels
	m_signalNames.push_back("din");
	m_channels.push_back(NULL);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

ChannelRenderer* EthernetProtocolDecoder::CreateRenderer()
{
	return new EthernetRenderer(this);
}

bool EthernetProtocolDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_ANALOG) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

bool EthernetProtocolDecoder::NeedsConfig()
{
	//No config needed
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual protocol decoding

void EthernetProtocolDecoder::BytesToFrames(
		vector<uint8_t>& bytes,
		vector<uint64_t>& starts,
		vector<uint64_t>& ends,
		EthernetCapture* cap)
{
	EthernetFrameSegment garbage;
	EthernetSample sample(-1, -1, garbage);	//ctor needs args even though we're gonna overwrite them
	sample.m_sample.m_type = EthernetFrameSegment::TYPE_INVALID;
	for(size_t i=0; i<bytes.size(); i++)
	{
		switch(sample.m_sample.m_type)
		{
			case EthernetFrameSegment::TYPE_INVALID:

				//In between frames. Look for a preamble
				if(bytes[i] != 0x55)
					LogDebug("EthernetProtocolDecoder: Skipping unknown byte %02x\n", bytes[i]);

				//Got a valid 55. We're now in the preamble
				sample.m_offset = starts[i] / cap->m_timescale;
				sample.m_sample.m_type = EthernetFrameSegment::TYPE_PREAMBLE;
				sample.m_sample.m_data.clear();
				sample.m_sample.m_data.push_back(0x55);
				break;

			case EthernetFrameSegment::TYPE_PREAMBLE:

				//TODO: Verify that this byte is immediately after the previous one

				//Look for the SFD
				if(bytes[i] == 0xd5)
				{
					//Save the preamble
					sample.m_duration = (starts[i] / cap->m_timescale) - sample.m_offset;
					cap->m_samples.push_back(sample);

					//Save the SFD
					sample.m_offset = starts[i] / cap->m_timescale;
					sample.m_duration = (ends[i] / cap->m_timescale) - sample.m_offset;
					sample.m_sample.m_type = EthernetFrameSegment::TYPE_SFD;
					sample.m_sample.m_data.clear();
					sample.m_sample.m_data.push_back(0xd5);
					cap->m_samples.push_back(sample);

					//Set up for data
					sample.m_sample.m_type = EthernetFrameSegment::TYPE_DST_MAC;
					sample.m_sample.m_data.clear();
				}

				//No SFD, just add the preamble byte
				else if(bytes[i] == 0x55)
					sample.m_sample.m_data.push_back(0x55);

				//Garbage (TODO: handle this better)
				else
					LogDebug("EthernetProtocolDecoder: Skipping unknown byte %02x\n", bytes[i]);

				break;

			case EthernetFrameSegment::TYPE_DST_MAC:

				//Start of MAC? Record start time
				if(sample.m_sample.m_data.empty())
					sample.m_offset = starts[i] / cap->m_timescale;

				//Add the data
				sample.m_sample.m_data.push_back(bytes[i]);

				//Are we done? Add it
				if(sample.m_sample.m_data.size() == 6)
				{
					sample.m_duration = (ends[i] / cap->m_timescale) - sample.m_offset;
					cap->m_samples.push_back(sample);

					//Reset for next block of the frame
					sample.m_sample.m_type = EthernetFrameSegment::TYPE_SRC_MAC;
					sample.m_sample.m_data.clear();
				}

				break;

			case EthernetFrameSegment::TYPE_SRC_MAC:

				//Start of MAC? Record start time
				if(sample.m_sample.m_data.empty())
					sample.m_offset = starts[i] / cap->m_timescale;

				//Add the data
				sample.m_sample.m_data.push_back(bytes[i]);

				//Are we done? Add it
				if(sample.m_sample.m_data.size() == 6)
				{
					sample.m_duration = (ends[i] / cap->m_timescale) - sample.m_offset;
					cap->m_samples.push_back(sample);

					//Reset for next block of the frame
					sample.m_sample.m_type = EthernetFrameSegment::TYPE_ETHERTYPE;
					sample.m_sample.m_data.clear();
				}

				break;

			case EthernetFrameSegment::TYPE_ETHERTYPE:

				//Start of Ethertype? Record start time
				if(sample.m_sample.m_data.empty())
					sample.m_offset = starts[i] / cap->m_timescale;

				//Add the data
				sample.m_sample.m_data.push_back(bytes[i]);

				//Are we done? Add it
				if(sample.m_sample.m_data.size() == 2)
				{
					sample.m_duration = (ends[i] / cap->m_timescale) - sample.m_offset;
					cap->m_samples.push_back(sample);

					//Reset for next block of the frame
					sample.m_sample.m_type = EthernetFrameSegment::TYPE_PAYLOAD;
					sample.m_sample.m_data.clear();
				}

				break;

			case EthernetFrameSegment::TYPE_PAYLOAD:

				//Add a data element
				//For now, each byte is its own payload blob
				sample.m_offset = starts[i] / cap->m_timescale;
				sample.m_duration = (ends[i] / cap->m_timescale) - sample.m_offset;
				sample.m_sample.m_type = EthernetFrameSegment::TYPE_PAYLOAD;
				sample.m_sample.m_data.clear();
				sample.m_sample.m_data.push_back(bytes[i]);
				cap->m_samples.push_back(sample);

				//If almost at end of packet, next 4 bytes are FCS
				if(i == bytes.size() - 5)
				{
					sample.m_sample.m_data.clear();
					sample.m_sample.m_type = EthernetFrameSegment::TYPE_FCS;
				}
				break;

			case EthernetFrameSegment::TYPE_FCS:

				//Start of FCS? Record start time
				if(sample.m_sample.m_data.empty())
					sample.m_offset = starts[i] / cap->m_timescale;

				//Add the data
				sample.m_sample.m_data.push_back(bytes[i]);

				//Are we done? Add it
				if(sample.m_sample.m_data.size() == 4)
				{
					sample.m_duration = (ends[i] / cap->m_timescale) - sample.m_offset;
					cap->m_samples.push_back(sample);
				}

				break;

			default:
				break;
		}
	}
}
