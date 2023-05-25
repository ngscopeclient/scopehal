/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2023 Andrew D. Zonenberg and contributors                                                         *
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
#include "Ethernet10GbaseRDecoder.h"
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

Ethernet10GBaseRDecoder::Ethernet10GBaseRDecoder(const string& color)
	: EthernetProtocolDecoder(color)
{
	//Digital inputs, so need to undo some stuff for the PHY layer decodes
	m_signalNames.clear();
	m_inputs.clear();

	//Add inputs. We take a single 64b66b coded stream
	CreateInput("data");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string Ethernet10GBaseRDecoder::GetProtocolName()
{
	return "Ethernet - 10GBaseR";
}

bool Ethernet10GBaseRDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (dynamic_cast<Ethernet64b66bWaveform*>(stream.m_channel->GetData(0)) != NULL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void Ethernet10GBaseRDecoder::Refresh()
{
	ClearPackets();

	//Get the input data
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	auto data = dynamic_cast<Ethernet64b66bWaveform*>(GetInputWaveform(0));
	data->PrepareForCpuAccess();

	//Create the output capture
	auto cap = new EthernetWaveform;
	cap->m_timescale = data->m_timescale;
	cap->m_startTimestamp = data->m_startTimestamp;
	cap->m_startFemtoseconds = data->m_startFemtoseconds;
	cap->PrepareForCpuAccess();

	size_t len = data->m_samples.size();
	for(size_t i=0; i < len; i++)
	{
		//Ignore idles and autonegotiation for now

		auto symbol = data->m_samples[i];

		//Set of recovered bytes and timestamps
		vector<uint8_t> bytes;
		vector<uint64_t> starts;
		vector<uint64_t> ends;

		//We should always start with a control field
		if(symbol.m_header != 2)
			continue;

		//Pull out the control field type and see what it is
		uint8_t type = (symbol.m_data >> 56) & 0xff;

		//Calculate byte offsets for each byte within the codeword
		uint64_t dlen = data->m_durations[i] / 8;
		uint8_t vbytes[8] = {0};
		uint64_t offsets[8] = {0};
		uint64_t durations[8] = {0};
		for(size_t j=0; j<8; j++)
		{
			vbytes[j]		= (symbol.m_data >> (56 - j*8)) & 0xff;
			offsets[j]		= data->m_offsets[i] + dlen*j;
			durations[j]	= dlen;
		}
		durations[7] = data->m_durations[i] + offsets[0] - offsets[7];	//fit any roundoff error in here

		switch(type)
		{
			//Eight control fields
			case 0x1e:
				//for now, assume it's an idle
				continue;

			//TODO: handle 0x66 ordered set

			//Three data bytes, ordered set code, ordered set code, three data bytes
			case 0x55:
				{
					//Byte 4 defines ordered set code.
					//Must be 0x00
					if(vbytes[4] != 0x00)
					{
						cap->m_offsets.push_back(offsets[4]);
						cap->m_durations.push_back(durations[4]);
						cap->m_samples.push_back(EthernetFrameSegment(EthernetFrameSegment::TYPE_INVALID, 0));
						continue;
					}

					//It's two ordered sets. All supported ordered sets start with 0x00 00, verify that.
					if( (vbytes[1] != 0x00) || (vbytes[2] != 0x00) || (vbytes[5] != 0x00) || (vbytes[6] != 0x00) )
					{
						cap->m_offsets.push_back(offsets[4]);
						cap->m_durations.push_back(durations[4]);
						cap->m_samples.push_back(EthernetFrameSegment(EthernetFrameSegment::TYPE_INVALID, 0));
						continue;
					}

					//Different ordered set code in each half? Not currently implemented
					if(vbytes[3] != vbytes[7])
					{
						continue;
					}

					//Same ordered set code in both half? Decode it
					EthernetFrameSegment::SegmentType vtype;
					switch(vbytes[3])
					{
						case 0x01:
							vtype = EthernetFrameSegment::TYPE_LOCAL_FAULT;
							break;

						case 0x02:
							vtype = EthernetFrameSegment::TYPE_REMOTE_FAULT;
							break;

						case 0x03:
							vtype = EthernetFrameSegment::TYPE_LINK_INTERRUPTION;
							break;

						default:
							vtype = EthernetFrameSegment::TYPE_INVALID;
							break;

					}

					//If we have two consecutive identical samples, extend them
					if(!cap->m_samples.empty())
					{
						auto n = cap->m_samples.size()-1;
						if(cap->m_samples[n].m_type == vtype)
						{
							cap->m_durations[n] = offsets[7] + durations[7] - cap->m_offsets[n];
							continue;
						}
					}

					//Add sample
					cap->m_offsets.push_back(offsets[0]);
					cap->m_durations.push_back(offsets[7] + durations[7] - offsets[0]);
					cap->m_samples.push_back(EthernetFrameSegment(vtype, 0));
				}
				break;

			//Four control fields, four padding bits, start of frame, 3 data bytes
			//In other words, frame starts in the second half of the block
			case 0x33:
				{
					//Synthesize an 0x55 from the implied SOF
					bytes.push_back(0x55);
					starts.push_back(offsets[4]);
					ends.push_back(offsets[4] + durations[4]);

					for(int j=0; j<3; j++)
					{
						bytes.push_back(vbytes[5+j]);
						starts.push_back(offsets[5+j]);
						ends.push_back(offsets[5+j] + durations[5+j]);
					}
				}
				break;

			//Start of frame, 7 data bytes
			//In other words, frame starts at the beginning of the block
			case 0x78:
				{
					//Synthesize an 0x55 from the implied SOF
					bytes.push_back(0x55);
					starts.push_back(offsets[0]);
					ends.push_back(offsets[0] + durations[0]);

					for(int j=0; j<7; j++)
					{
						bytes.push_back(vbytes[1+j]);
						starts.push_back(offsets[1+j]);
						ends.push_back(offsets[1+j] + durations[1+j]);
					}
				}
				break;

			//Anything else isn't interesting, skip it
			default:
				continue;
		}

		//Skip the start symbol
		i++;

		//Decode frame data until we see a control or error character.
		//Any control character would mean end-of-frame or error.
		while(i < len)
		{
			//Extract the symbol and crack it into bytes
			symbol = data->m_samples[i];
			dlen = data->m_durations[i] / 8;
			for(size_t j=0; j<8; j++)
			{
				vbytes[j]		= (symbol.m_data >> (56 - j*8)) & 0xff;
				offsets[j]		= data->m_offsets[i] + dlen*j;
				durations[j]	= dlen;
			}
			durations[7] = data->m_durations[i] + offsets[0] - offsets[7];

			//Figure out what we're dealing with

			//Eight data bytes
			if(symbol.m_header == 1)
			{
				for(int j=0; j<8; j++)
				{
					bytes.push_back(vbytes[j]);
					starts.push_back(offsets[j]);
					ends.push_back(offsets[j] + durations[j]);
				}
			}

			//Control symbols always end the frame.
			//For now we assume control fields are always "end of frame" followed by idles
			//TODO: handle other possibilities here
			else if(symbol.m_header == 2)
			{
				//The big question is, how many data octets are present
				switch(vbytes[0])
				{
					//7 control fields, no data
					case 0x87:
						break;

					//One data octet, six control fields
					case 0x99:
						bytes.push_back(vbytes[1]);
						starts.push_back(offsets[1]);
						ends.push_back(offsets[1] + durations[1]);
						break;

					//Two data octets, five control fields
					case 0xaa:
						for(size_t j=0; j<2; j++)
						{
							bytes.push_back(vbytes[1+j]);
							starts.push_back(offsets[1+j]);
							ends.push_back(offsets[1+j] + durations[1+j]);
						}
						break;

					//Three data octets, four control fields
					case 0xb4:
						for(size_t j=0; j<3; j++)
						{
							bytes.push_back(vbytes[1+j]);
							starts.push_back(offsets[1+j]);
							ends.push_back(offsets[1+j] + durations[1+j]);
						}
						break;

					//Four data octets, three control fields
					case 0xcc:
						for(size_t j=0; j<4; j++)
						{
							bytes.push_back(vbytes[1+j]);
							starts.push_back(offsets[1+j]);
							ends.push_back(offsets[1+j] + durations[1+j]);
						}
						break;

					//Five data octets, two control fields
					case 0xd2:
						for(size_t j=0; j<5; j++)
						{
							bytes.push_back(vbytes[1+j]);
							starts.push_back(offsets[1+j]);
							ends.push_back(offsets[1+j] + durations[1+j]);
						}
						break;

					//Six data octets, one control field
					case 0xe1:
						for(size_t j=0; j<6; j++)
						{
							bytes.push_back(vbytes[1+j]);
							starts.push_back(offsets[1+j]);
							ends.push_back(offsets[1+j] + durations[1+j]);
						}
						break;

					//Seven data octets
					case 0xff:
						for(size_t j=0; j<7; j++)
						{
							bytes.push_back(vbytes[1+j]);
							starts.push_back(offsets[1+j]);
							ends.push_back(offsets[1+j] + durations[1+j]);
						}
						break;
				}

				break;
			}

			//Invalid. Abort the frame
			else
				break;

			i++;
		}


		//TODO: if error, create a single giant "ERROR" frame block? or what

		//Crunch the data
		BytesToFrames(bytes, starts, ends, cap);
	}

	SetData(cap, 0);
	cap->MarkModifiedFromCpu();
}
