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
	@brief Implementation of PCIeTransportDecoder
 */
#include "../scopehal/scopehal.h"
#include "PCIeDataLinkDecoder.h"
#include "PCIeTransportDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PCIeTransportDecoder::PCIeTransportDecoder(const string& color)
	: PacketDecoder(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_BUS)
{
	//Set up channels
	CreateInput("link");
}

PCIeTransportDecoder::~PCIeTransportDecoder()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool PCIeTransportDecoder::NeedsConfig()
{
	//No config needed
	return false;
}

bool PCIeTransportDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (dynamic_cast<PCIeDataLinkWaveform*>(stream.m_channel->GetData(0)) != NULL) )
		return true;

	return false;
}

string PCIeTransportDecoder::GetProtocolName()
{
	return "PCIe Transport";
}

void PCIeTransportDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "PCIETransport(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void PCIeTransportDecoder::Refresh()
{
	ClearPackets();

	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}
	auto data = dynamic_cast<PCIeDataLinkWaveform*>(GetInputWaveform(0));

	//Create the capture
	auto cap = new PCIeTransportWaveform;
	cap->m_timescale = data->m_timescale;
	cap->m_startTimestamp = data->m_startTimestamp;
	cap->m_startPicoseconds = data->m_startPicoseconds;
	SetData(cap, 0);

	enum
	{
		STATE_IDLE,
		STATE_HEADER_0,
		STATE_HEADER_1
	} state = STATE_IDLE;

	size_t len = data->m_samples.size();

	Packet* pack = NULL;

	enum TLPFormat
	{
		TLP_FORMAT_3W_NODATA	= 0,
		TLP_FORMAT_4W_NODATA	= 1,
		TLP_FORMAT_3W_DATA		= 2,
		TLP_FORMAT_4W_DATA 		= 3
	} tlp_format;

	bool format_4word = false;
	bool has_data = false;

	for(size_t i=0; i<len; i++)
	{
		auto sym = data->m_samples[i];
		int64_t off = data->m_offsets[i];
		int64_t dur = data->m_durations[i];
		int64_t halfdur = dur/2;
		int64_t end = off + dur;

		switch(state)
		{
			////////////////////////////////////////////////////////////////////////////////////////////////////////////
			// Wait for a packet to start

			case STATE_IDLE:

				//Ignore everything but start of a TLP
				if(sym.m_type == PCIeDataLinkSymbol::TYPE_TLP_SEQUENCE)
				{
					//Create the packet
					pack = new Packet;
					m_packets.push_back(pack);
					pack->m_offset = off * cap->m_timescale;
					pack->m_len = 0;
					pack->m_headers["Seq"] = to_string(sym.m_data);

					state = STATE_HEADER_0;
				}

				break;	//end STATE_IDLE

			////////////////////////////////////////////////////////////////////////////////////////////////////////////
			// TLP headers

			case STATE_HEADER_0:

				if(sym.m_type != PCIeDataLinkSymbol::TYPE_TLP_DATA)
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_ERROR));
					state = STATE_IDLE;
				}

				else
				{
					//Extract format
					tlp_format = static_cast<TLPFormat>(sym.m_data >> 5);
					format_4word = (tlp_format == TLP_FORMAT_4W_NODATA) || (tlp_format == TLP_FORMAT_4W_DATA);
					has_data = (tlp_format == TLP_FORMAT_3W_DATA) || (tlp_format == TLP_FORMAT_4W_DATA);

					//Type is a bit complicated, because it depends on both type and format fields
					PCIeTransportSymbol::TlpType type = PCIeTransportSymbol::TYPE_INVALID;
					pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_ERROR];
					switch(sym.m_data & 0x1f)
					{
						case 0:
							if(!has_data)
							{
								type = PCIeTransportSymbol::TYPE_MEM_RD;
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							}
							else
							{
								type = PCIeTransportSymbol::TYPE_MEM_WR;
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
							}
							break;

						case 1:
							if(!has_data)
							{
								type = PCIeTransportSymbol::TYPE_MEM_RD_LK;
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							}
							break;

						case 2:
							if(tlp_format == TLP_FORMAT_3W_NODATA)
							{
								type = PCIeTransportSymbol::TYPE_IO_RD;
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_CONTROL];
							}
							else if(tlp_format == TLP_FORMAT_3W_DATA)
							{
								type = PCIeTransportSymbol::TYPE_IO_WR;
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_CONTROL];
							}
							break;

						//Type 3 appears unallocated, not mentioned in the spec

						case 4:
							if(tlp_format == TLP_FORMAT_3W_NODATA)
							{
								type = PCIeTransportSymbol::TYPE_CFG_RD_0;
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_CONTROL];
							}
							else if(tlp_format == TLP_FORMAT_3W_DATA)
							{
								type = PCIeTransportSymbol::TYPE_CFG_WR_0;
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_CONTROL];
							}
							break;

						case 5:
							if(tlp_format == TLP_FORMAT_3W_NODATA)
							{
								type = PCIeTransportSymbol::TYPE_CFG_RD_1;
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_CONTROL];
							}
							else if(tlp_format == TLP_FORMAT_3W_DATA)
							{
								type = PCIeTransportSymbol::TYPE_CFG_WR_1;
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_CONTROL];
							}
							break;

						//Type 0x1b is deprecated

						case 10:
							if(tlp_format == TLP_FORMAT_3W_NODATA)
							{
								type = PCIeTransportSymbol::TYPE_COMPLETION;
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_STATUS];
							}
							else if(tlp_format == TLP_FORMAT_3W_DATA)
							{
								type = PCIeTransportSymbol::TYPE_COMPLETION_DATA;
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							}
							break;

						case 11:
							if(tlp_format == TLP_FORMAT_3W_NODATA)
							{
								type = PCIeTransportSymbol::TYPE_COMPLETION_LOCKED_ERROR;
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_STATUS];
							}
							else if(tlp_format == TLP_FORMAT_3W_DATA)
							{
								type = PCIeTransportSymbol::TYPE_COMPLETION_LOCKED_DATA;
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
							}
							break;
					}

					//TODO: support Msg / MSgD

					//Add the type symbol
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeTransportSymbol(PCIeTransportSymbol::TYPE_TLP_TYPE, type));

					pack->m_headers["Type"] = GetText(cap->m_samples.size()-1);

					state = STATE_HEADER_1;
				}

				break;	//end STATE_HEADER_0
		}
	}
}

Gdk::Color PCIeTransportDecoder::GetColor(int i)
{
	auto capture = dynamic_cast<PCIeTransportWaveform*>(GetData(0));
	if(capture != NULL)
	{
		auto s = capture->m_samples[i];

		switch(s.m_type)
		{
			case PCIeTransportSymbol::TYPE_TLP_TYPE:
				return m_standardColors[COLOR_CONTROL];

			case PCIeTransportSymbol::TYPE_ERROR:
			default:
				return m_standardColors[COLOR_ERROR];
		}
	}

	return m_standardColors[COLOR_ERROR];
}

string PCIeTransportDecoder::GetText(int i)
{
	char tmp[64];

	auto capture = dynamic_cast<PCIeTransportWaveform*>(GetData(0));
	if(capture != NULL)
	{
		auto s = capture->m_samples[i];

		switch(s.m_type)
		{
			case PCIeTransportSymbol::TYPE_TLP_TYPE:
				switch(s.m_data)
				{
					case PCIeTransportSymbol::TYPE_MEM_RD:			return "Mem read";
					case PCIeTransportSymbol::TYPE_MEM_RD_LK:		return "Mem read locked";
					case PCIeTransportSymbol::TYPE_MEM_WR:			return "Mem write";
					case PCIeTransportSymbol::TYPE_IO_RD:			return "IO read";
					case PCIeTransportSymbol::TYPE_IO_WR:			return "IO write";
					case PCIeTransportSymbol::TYPE_CFG_RD_0:		return "Cfg read 0";
					case PCIeTransportSymbol::TYPE_CFG_WR_0:		return "Cfg write 0";
					case PCIeTransportSymbol::TYPE_CFG_RD_1:		return "Cfg read 1";
					case PCIeTransportSymbol::TYPE_CFG_WR_1:		return "Cfg write 1";

					case PCIeTransportSymbol::TYPE_MSG:
					case PCIeTransportSymbol::TYPE_MSG_DATA:
						return "Message";

					case PCIeTransportSymbol::TYPE_COMPLETION:
					case PCIeTransportSymbol::TYPE_COMPLETION_DATA:
						return "Completion";

					case PCIeTransportSymbol::TYPE_COMPLETION_LOCKED_ERROR:
					case PCIeTransportSymbol::TYPE_COMPLETION_LOCKED_DATA:
						return "Completion locked";

					case PCIeTransportSymbol::TYPE_INVALID:
					default:
						return "ERROR";
				}

			case PCIeTransportSymbol::TYPE_ERROR:
			default:
				return "ERROR";
		}
	}
	return "";
}

vector<string> PCIeTransportDecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("Seq");
	ret.push_back("Type");

	/*
	ret.push_back("Seq");
	ret.push_back("HdrFC");
	ret.push_back("DataFC");
	*/
	ret.push_back("Length");
	return ret;
}
