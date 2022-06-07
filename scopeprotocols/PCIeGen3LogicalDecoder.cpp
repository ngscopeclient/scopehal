/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
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
	@brief Implementation of PCIeGen3LogicalDecoder
 */
#include "../scopehal/scopehal.h"
#include "../scopehal/Filter.h"
#include "PCIe128b130bDecoder.h"
#include "PCIeGen3LogicalDecoder.h"


using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PCIeGen3LogicalDecoder::PCIeGen3LogicalDecoder(const string& color)
	: PCIeGen2LogicalDecoder(color)
{
}

PCIeGen3LogicalDecoder::~PCIeGen3LogicalDecoder()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool PCIeGen3LogicalDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	size_t nports = m_parameters[m_portCountName].GetIntVal();
	if( (i <= nports) && (dynamic_cast<PCIe128b130bWaveform*>(stream.m_channel->GetData(0)) != NULL) )
		return true;

	return false;
}

string PCIeGen3LogicalDecoder::GetProtocolName()
{
	return "PCIe Gen 3/4/5 Logical";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void PCIeGen3LogicalDecoder::Refresh()
{
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get all of the inputs
	ssize_t nports = m_parameters[m_portCountName].GetIntVal();
	vector<PCIe128b130bWaveform*> inputs;
	for(ssize_t i=0; i<nports; i++)
		inputs.push_back(dynamic_cast<PCIe128b130bWaveform*>(GetInputWaveform(i)));

	if(nports == 0)
	{
		SetData(NULL, 0);
		return;
	}

	//Create the capture
	//Output is time aligned with the input
	auto cap = new PCIeLogicalWaveform;
	auto in0 = inputs[0];
	cap->m_timescale = 1;
	cap->m_startTimestamp = in0->m_startTimestamp;
	cap->m_startFemtoseconds = in0->m_startFemtoseconds;
	cap->m_triggerPhase = 0;

	//Find the first skip ordered set in each lane so we can synchronize them to each other
	//TODO: this might fail if we have a partial set of SOS's right at the start of the capture and there's a few
	//symbols worth of skew between the probes.
	//We can improve reliability by searching for the second comma in this case.
	vector<size_t> indexes;
	for(ssize_t i=0; i<nports; i++)
	{
		auto in = inputs[i];

		size_t len = in->m_samples.size();
		size_t j=0;
		for(; j<len; j++)
		{
			auto sym = in->m_samples[j];
			if( (sym.m_type == PCIe128b130bSymbol::TYPE_ORDERED_SET) && (sym.m_data[0] == 0xaa) )
				break;
		}

		indexes.push_back(j);
	}

	//Add "scrambler desynced" symbol from start of waveform until the first skip set in lane 0
	int64_t symstart = (in0->m_offsets[indexes[0]] * in0->m_timescale) + in0->m_triggerPhase;
	cap->m_offsets.push_back(0);
	cap->m_durations.push_back(symstart);
	cap->m_samples.push_back(PCIeLogicalSymbol(PCIeLogicalSymbol::TYPE_NO_SCRAMBLER));

	//Pass through the skip ordered set
	cap->m_offsets.push_back(symstart);
	cap->m_durations.push_back(in0->m_durations[indexes[0]] * in0->m_timescale);
	cap->m_samples.push_back(PCIeLogicalSymbol(PCIeLogicalSymbol::TYPE_SKIP));

	//Process the input, one striped symbol at a time
	enum
	{
		PACKET_STATE_IDLE,
		PACKET_STATE_START_DLLP,
		PACKET_STATE_DLLP,
		PACKET_STATE_EDS_1,
		PACKET_STATE_EDS_2,
		PACKET_STATE_EDS_3,
		PACKET_STATE_STP_1,
		PACKET_STATE_TLP_DATA,
		PACKET_STATE_EDB
	} packet_state = PACKET_STATE_IDLE;
	int64_t count = 0;
	int64_t packet_len = 0;
	while(true)
	{
		//Get bounds of each logical byte within the stream
		size_t i0 = indexes[0];
		symstart = (in0->m_offsets[i0] * in0->m_timescale) + in0->m_triggerPhase;
		int64_t symlen = (in0->m_durations[i0] * in0->m_timescale);
		int64_t sublen = symlen / (nports * 16);

		//Process ordered sets (on all lanes at once)
		//For now, assume we're synced across all lanes.
		//TODO: better handling of protocol errors where ordered sets desync
		if(inputs[0]->m_samples[i0].m_type == PCIe128b130bSymbol::TYPE_ORDERED_SET)
		{
			switch(inputs[0]->m_samples[i0].m_data[0])
			{
				//SOS
				case 0xaa:
					cap->m_offsets.push_back(symstart);
					cap->m_durations.push_back(symlen);
					cap->m_samples.push_back(PCIeLogicalSymbol(PCIeLogicalSymbol::TYPE_SKIP));
					break;

				//Electrical Idle Exit EIEOS
				//Electrical Idle EIOS
				//TODO: handle this
				case 0x00:
				case 0x66:
					AddLogicalIdle(cap, symstart, symstart+symlen);
					break;

				//FTS Fast Training Sequence
				case 0x55:
					AddLogicalIdle(cap, symstart, symstart+symlen);
					break;

				//TS1 Training Sequence 1
				//TS2 Training Sequence 2
				case 0x1e:
				case 0x2d:
					AddLogicalIdle(cap, symstart, symstart+symlen);
					break;

				//Start of Data Stream SDS
				case 0xe1:
					AddLogicalIdle(cap, symstart, symstart+symlen);
					break;

				//TODO: other ordered sets
				default:
					cap->m_offsets.push_back(symstart);
					cap->m_durations.push_back(symlen);
					cap->m_samples.push_back(PCIeLogicalSymbol(PCIeLogicalSymbol::TYPE_ERROR));
					break;
			}
		}

		else
		{
			//Process data
			//Bytes are striped across lanes *within* 128/130 blocks!
			for(ssize_t k=0; k<16; k++)
			{
				for(ssize_t j=0; j<nports; j++)
				{
					auto data = inputs[j];
					auto i = indexes[j];

					size_t len = cap->m_offsets.size();

					//Figure out bounds of byte within the physical layer symbols
					auto sym = data->m_samples[i];
					int64_t off = symstart + (k*nports + j)*sublen;
					int64_t dur = sublen;
					int64_t end = off + sublen;
					if( (k == 15) && (j == nports-1) )
					{
						end = symstart + symlen;
						dur = end - off;
					}

					bool error = false;

					//Pass through errors
					if(data->m_samples[i].m_type == PCIe128b130bSymbol::TYPE_ERROR)
						error = true;

					else
					{
						switch(packet_state)
						{
							//Not in packet? Expect idle or framing token
							case PACKET_STATE_IDLE:
								{
									bool found = true;
									switch(sym.m_data[k])
									{
										//IDL 00
										case 0x00:
											AddLogicalIdle(cap, off, end);
											break;

										//SDP F0 AC
										case 0xf0:
											cap->m_offsets.push_back(off);
											cap->m_durations.push_back(dur);
											cap->m_samples.push_back(PCIeLogicalSymbol(
												PCIeLogicalSymbol::TYPE_START_DLLP));
											packet_state = PACKET_STATE_START_DLLP;
											break;

										//EDS 1F 80 90 00
										case 0x1f:
											cap->m_offsets.push_back(off);
											cap->m_durations.push_back(dur);
											cap->m_samples.push_back(PCIeLogicalSymbol(
												PCIeLogicalSymbol::TYPE_END_DATA_STREAM));
											packet_state = PACKET_STATE_EDS_1;
											break;

										//EDB 0xc0 c0 c0 c0
										case 0xc0:
											cap->m_offsets.push_back(off);
											cap->m_durations.push_back(dur);
											cap->m_samples.push_back(PCIeLogicalSymbol(
												PCIeLogicalSymbol::TYPE_END_BAD));
											packet_state = PACKET_STATE_EDB;
											count = 0;
											break;

										//Invalid
										default:
											found = false;
									}

									//STP *F (high 4 bits are low 4 bits of length)
									if(found)
									{}

									else if( (sym.m_data[k] & 0x0f) == 0x0f)
									{
										count = 0;
										packet_len = sym.m_data[k] >> 4;
										packet_state = PACKET_STATE_STP_1;

										cap->m_offsets.push_back(off);
										cap->m_durations.push_back(dur);
										cap->m_samples.push_back(PCIeLogicalSymbol(
											PCIeLogicalSymbol::TYPE_START_TLP));
									}

									else
										error = true;
								}
								break;

							////////////////////////////////////////////////////////////////////////////////////////////////
							// DLLP path

							//Expect second word of SDP token
							case PACKET_STATE_START_DLLP:
								if(sym.m_data[k] == 0xac)
								{
									cap->m_durations[len-1] = end - cap->m_offsets[len-1];
									count = 0;
									packet_state = PACKET_STATE_DLLP;
								}

								//Malformed SDP token
								else
									error = true;
								break;

							//DLLP content (6 bytes)
							case PACKET_STATE_DLLP:
								cap->m_offsets.push_back(off);
								cap->m_durations.push_back(dur);
								cap->m_samples.push_back(PCIeLogicalSymbol(
									PCIeLogicalSymbol::TYPE_PAYLOAD_DATA, sym.m_data[k]));

								count++;
								if(count == 6)
									packet_state = PACKET_STATE_IDLE;

								break;

							////////////////////////////////////////////////////////////////////////////////////////////////
							// STP token path

							//Second half of TLP length
							case PACKET_STATE_STP_1:

								//Extend previous symbol
								cap->m_durations[len-1] = end - cap->m_offsets[len-1];
								packet_len |= ((sym.m_data[k] & 0x7f) << 4);

								//packet length in header is dwords, convert to bytes
								packet_len *= 4;

								//sequence number doesn't count
								packet_len -= 2;

								//TODO: check frame parity bit
								packet_state = PACKET_STATE_TLP_DATA;
								break;

							//TLP content
							case PACKET_STATE_TLP_DATA:

								count++;
								if(count == packet_len)
								{
									//Add an end symbol so the data link layer knows the frame ended
									//(even though there's not an explicit one in the gen3 line coding)
									auto halflen = dur/2;
									cap->m_offsets.push_back(off);
									cap->m_durations.push_back(halflen);
									cap->m_samples.push_back(PCIeLogicalSymbol(
										PCIeLogicalSymbol::TYPE_PAYLOAD_DATA, sym.m_data[k]));

									cap->m_offsets.push_back(off + halflen);
									cap->m_durations.push_back(dur - halflen);
									cap->m_samples.push_back(PCIeLogicalSymbol(
										PCIeLogicalSymbol::TYPE_END));

									packet_state = PACKET_STATE_IDLE;
								}

								else
								{
									cap->m_offsets.push_back(off);
									cap->m_durations.push_back(dur);
									cap->m_samples.push_back(PCIeLogicalSymbol(
										PCIeLogicalSymbol::TYPE_PAYLOAD_DATA, sym.m_data[k]));
								}
								break;

							////////////////////////////////////////////////////////////////////////////////////////////////
							// EDS token path

							//Expect second word of EDS token
							case PACKET_STATE_EDS_1:
								if(sym.m_data[k] == 0x80)
								{
									cap->m_durations[len-1] = end - cap->m_offsets[len-1];
									packet_state = PACKET_STATE_EDS_2;
								}

								//Malformed SDP token
								else
									error = true;
								break;

							//Expect third word of EDS token
							case PACKET_STATE_EDS_2:
								if(sym.m_data[k] == 0x90)
								{
									cap->m_durations[len-1] = end - cap->m_offsets[len-1];
									packet_state = PACKET_STATE_EDS_3;
								}

								//Malformed SDP token
								else
									error = true;
								break;

							//Expect fourth word of EDS token
							case PACKET_STATE_EDS_3:
								if(sym.m_data[k] == 0x00)
								{
									cap->m_durations[len-1] = end - cap->m_offsets[len-1];
									packet_state = PACKET_STATE_IDLE;
								}

								//Malformed SDP token
								else
									error = true;
								break;

							////////////////////////////////////////////////////////////////////////////////////////////////
							// EDB token path

							case PACKET_STATE_EDB:
								if(sym.m_data[k] == 0xc0)
								{
									cap->m_durations[len-1] = end - cap->m_offsets[len-1];
									count ++;

									if(count == 3)
										packet_state = PACKET_STATE_IDLE;
								}

								//Malformed EDB token
								else
									error = true;
								break;
						}
					}

					if(error)
					{
						cap->m_offsets.push_back(off);
						cap->m_durations.push_back(dur);
						cap->m_samples.push_back(PCIeLogicalSymbol(PCIeLogicalSymbol::TYPE_ERROR));
						packet_state = PACKET_STATE_IDLE;
					}
				}
			}
		}

		//Increment indexes and check if we went off the end of any of the input streams
		bool done = false;
		for(ssize_t j=0; j<nports; j++)
		{
			indexes[j] ++;

			if(indexes[j] >= inputs[j]->m_samples.size())
				done = true;
		}
		if(done)
			break;
	}

	SetData(cap, 0);
}

/**
	@brief Adds a logical idle symbol, or extends an existing one
 */
void PCIeGen3LogicalDecoder::AddLogicalIdle(PCIeLogicalWaveform* cap, int64_t off, int64_t tend)
{
	size_t len = cap->m_offsets.size();

	if(len > 0)
	{
		if(cap->m_samples[len-1].m_type == PCIeLogicalSymbol::TYPE_LOGICAL_IDLE)
		{
			cap->m_durations[len-1] = tend - cap->m_offsets[len-1];
			return;
		}
	}

	cap->m_offsets.push_back(off);
	cap->m_durations.push_back(tend - off);
	cap->m_samples.push_back(PCIeLogicalSymbol(PCIeLogicalSymbol::TYPE_LOGICAL_IDLE));
}
