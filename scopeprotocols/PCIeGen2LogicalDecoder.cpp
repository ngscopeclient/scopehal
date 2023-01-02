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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Implementation of PCIeGen2LogicalDecoder
 */
#include "../scopehal/scopehal.h"
#include "../scopehal/Filter.h"
#include "IBM8b10bDecoder.h"
#include "PCIeGen2LogicalDecoder.h"


using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PCIeGen2LogicalDecoder::PCIeGen2LogicalDecoder(const string& color)
	: Filter(color, CAT_BUS)
	, m_portCountName("Lane Count")
{
	AddProtocolStream("data");
	m_parameters[m_portCountName] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_portCountName].SetIntVal(1);
	m_parameters[m_portCountName].signal_changed().connect(sigc::mem_fun(*this, &PCIeGen2LogicalDecoder::RefreshPorts));

	RefreshPorts();
}

PCIeGen2LogicalDecoder::~PCIeGen2LogicalDecoder()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool PCIeGen2LogicalDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	size_t nports = m_parameters[m_portCountName].GetIntVal();
	if( (i <= nports) && (dynamic_cast<IBM8b10bWaveform*>(stream.m_channel->GetData(0)) != NULL) )
		return true;

	return false;
}

string PCIeGen2LogicalDecoder::GetProtocolName()
{
	return "PCIe Gen 1/2 Logical";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void PCIeGen2LogicalDecoder::RefreshPorts()
{
	//Create new inputs
	size_t nports = m_parameters[m_portCountName].GetIntVal();
	for(size_t i=m_inputs.size(); i<nports; i++)
		CreateInput(string("Lane") + to_string(i+1));

	//Delete extra inputs
	for(size_t i=nports; i<m_inputs.size(); i++)
		SetInput(i, NULL, true);
	m_inputs.resize(nports);
	m_signalNames.resize(nports);

	m_inputsChangedSignal.emit();
}

void PCIeGen2LogicalDecoder::Refresh()
{
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get all of the inputs
	ssize_t nports = m_parameters[m_portCountName].GetIntVal();
	vector<IBM8b10bWaveform*> inputs;
	for(ssize_t i=0; i<nports; i++)
	{
		auto wfm = dynamic_cast<IBM8b10bWaveform*>(GetInputWaveform(i));
		inputs.push_back(wfm);
		wfm->PrepareForCpuAccess();
	}

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
	cap->PrepareForCpuAccess();

	//Find the first skip ordered set (K28.5 K28.0 K28.0 K28.0) in each lane so we can synchronize them to each other
	//TODO: this might fail if we have a partial set of them right at the start of the capture and there's a few symbols
	//worth of skew between the probes.
	//We can improve reliability by searching for the second set in this case.
	vector<size_t> indexes;
	vector<uint16_t> scramblers;
	for(ssize_t i=0; i<nports; i++)
	{
		auto in = inputs[i];

		size_t len = in->m_samples.size();
		size_t j=0;
		for(; j<len-3; j++)
		{
			if( (in->m_samples[j].m_control && (in->m_samples[j].m_data == 0xbc) ) &&
				(in->m_samples[j+1].m_control && (in->m_samples[j+1].m_data == 0x1c) ) &&
				(in->m_samples[j+2].m_control && (in->m_samples[j+2].m_data == 0x1c) ) &&
				(in->m_samples[j+3].m_control && (in->m_samples[j+3].m_data == 0x1c) ) )
			{
				break;
			}
		}

		indexes.push_back(j);
		scramblers.push_back(0xffff);
	}

	//Add "scrambler desynced" symbol from start of waveform until the first skip in lane 0
	cap->m_offsets.push_back(0);
	cap->m_durations.push_back(
		(in0->m_offsets[indexes[0]] + in0->m_durations[indexes[0]]) * in0->m_timescale +
		in0->m_triggerPhase);
	cap->m_samples.push_back(PCIeLogicalSymbol(PCIeLogicalSymbol::TYPE_NO_SCRAMBLER));

	//Process the input, one striped symbol at a time
	bool in_packet = false;
	while(true)
	{
		//Get bounds of each logical sub-symbol within the stream
		size_t i0 = indexes[0];
		int64_t symstart = (in0->m_offsets[i0] * in0->m_timescale) + in0->m_triggerPhase;
		int64_t symlen = (in0->m_durations[i0] * in0->m_timescale);
		int64_t sublen = symlen / nports;

		//Process data
		for(ssize_t j=0; j<nports; j++)
		{
			auto data = inputs[j];
			auto i = indexes[j];

			//Figure out bounds of sub-symbol
			auto sym = data->m_samples[i];
			int64_t off = symstart + sublen*j;
			int64_t dur = sublen;
			int64_t end = off + sublen;
			if(nports*sublen > symlen)
			{
				dur = symlen - (nports-1)*sublen;
				end = symstart + symlen;
			}

			//Figure out previous symbol type
			size_t outlen = cap->m_samples.size();
			size_t ilast = outlen - 1;
			bool last_was_skip = false;
			bool last_was_idle = false;
			bool last_was_pad = false;
			bool last_was_eidl = false;
			bool last_was_eie = false;
			if(outlen)
			{
				last_was_skip = (cap->m_samples[ilast].m_type == PCIeLogicalSymbol::TYPE_SKIP);
				last_was_pad = (cap->m_samples[ilast].m_type == PCIeLogicalSymbol::TYPE_PAD);
				last_was_idle = (cap->m_samples[ilast].m_type == PCIeLogicalSymbol::TYPE_LOGICAL_IDLE);
				last_was_eidl = (cap->m_samples[ilast].m_type == PCIeLogicalSymbol::TYPE_IDLE);
				last_was_eie = (cap->m_samples[ilast].m_type == PCIeLogicalSymbol::TYPE_EXIT_IDLE);
			}

			//Update the scrambler UNLESS we have a SKP character K28.0 (k.1c)
			uint8_t scrambler_out = 0;
			if(sym.m_control && (sym.m_data == 0x1c) )
			{}
			else
				scrambler_out = RunScrambler(scramblers[j]);

			//Control characters
			if(sym.m_control)
			{
				switch(sym.m_data)
				{
					//K28.5 COM
					case 0xbc:
						scramblers[j] = 0xffff;

						//Check for TSxx ordered sets
						//Don't decode them, we have a separate filter for that.
						//Focus is on identifying and not trying to descramble them.
						//X+10 to X+15 should be D10.2 (0x4a) for TS1 or D5.2 (0x45) for TS2
						if(i+15 < inputs[j]->m_samples.size() )
						{
							if(
								//Link ID must be K23.7 PAD or a D character
								( (data->m_samples[i+1].m_control && data->m_samples[i+1].m_data == 0xf7) ||
								(!data->m_samples[i+1].m_control) ) &&

								//Lane ID must be K23.7 or data character with value <= 31
								( (data->m_samples[i+2].m_control && data->m_samples[i+2].m_data == 0xf7) ||
								(!data->m_samples[i+2].m_control && (data->m_samples[i+2].m_data <= 31) ) ) &&

								//Rate ID must have bit 1 set
								(!data->m_samples[i+4].m_control && (data->m_samples[i+4].m_data & 2) ) )
							{
								bool hitTS1 = true;
								bool hitTS2 = true;

								for(size_t k=0; k<6; k++)
								{
									if(data->m_samples[i+10+k].m_control)
									{
										hitTS1 = false;
										hitTS2 = false;
										break;
									}

									if(data->m_samples[i+10+k].m_data != 0x4a)
										hitTS1 = false;
									if(data->m_samples[i+10+k].m_data != 0x45)
										hitTS2 = false;
								}

								auto setEnd = data->m_offsets[i+15] + data->m_durations[i+15];

								if(hitTS1)
								{
									cap->m_offsets.push_back(off);
									cap->m_durations.push_back(setEnd * data->m_timescale - off);
									cap->m_samples.push_back(PCIeLogicalSymbol(PCIeLogicalSymbol::TYPE_TS1));
								}

								if(hitTS2)
								{
									cap->m_offsets.push_back(off);
									cap->m_durations.push_back(setEnd * data->m_timescale - off);
									cap->m_samples.push_back(PCIeLogicalSymbol(PCIeLogicalSymbol::TYPE_TS2));
								}

								//Skip the rest of the ordered set
								if(hitTS1 || hitTS2)
								{
									indexes[j] += 15;

									//Cycle the LFSR for another 15 bytes
									for(size_t k=0; k<15; k++)
										RunScrambler(scramblers[j]);
								}
							}
						}
						break;

					//K28.0 SKP
					case 0x1c:
						{
							//Prefer to extend an existing symbol
							if(last_was_skip)
								cap->m_durations[ilast] = end - cap->m_offsets[outlen-1];

							//Nope, need to make a new symbol
							else
							{
								//If we had a gap from a COM character, stretch rearwards into it
								int64_t start = off;
								if(outlen)
									start = cap->m_offsets[ilast] + cap->m_durations[ilast];

								cap->m_offsets.push_back(start);
								cap->m_durations.push_back(end - start);
								cap->m_samples.push_back(PCIeLogicalSymbol(PCIeLogicalSymbol::TYPE_SKIP));
							}

							in_packet = false;
						}
						break;

					//K28.3 IDL
					case 0x7c:
						{
							//Extend the last symbol if it was one
							if(last_was_eidl)
								cap->m_durations[ilast] = end - cap->m_offsets[outlen-1];

							//Nope, need to make a new symbol
							else
							{
								cap->m_offsets.push_back(off);
								cap->m_durations.push_back(dur);
								cap->m_samples.push_back(PCIeLogicalSymbol(PCIeLogicalSymbol::TYPE_IDLE));
							}
						}
						break;

					//K28.7 EIE
					case 0xfc:
						{
							//Extend the last symbol if it was one
							if(last_was_eie)
								cap->m_durations[ilast] = end - cap->m_offsets[outlen-1];

							//Nope, need to make a new symbol
							else
							{
								cap->m_offsets.push_back(off);
								cap->m_durations.push_back(dur);
								cap->m_samples.push_back(PCIeLogicalSymbol(PCIeLogicalSymbol::TYPE_EXIT_IDLE));
							}
						}
						break;

					//K28.2 SDP
					case 0x5c:
						cap->m_offsets.push_back(off);
						cap->m_durations.push_back(dur);
						cap->m_samples.push_back(PCIeLogicalSymbol(PCIeLogicalSymbol::TYPE_START_DLLP));
						in_packet = true;
						break;

					//K27.7 STP
					case 0xfb:
						cap->m_offsets.push_back(off);
						cap->m_durations.push_back(dur);
						cap->m_samples.push_back(PCIeLogicalSymbol(PCIeLogicalSymbol::TYPE_START_TLP));
						in_packet = true;
						break;

					//K23.7 PAD
					case 0xf7:
						{
							//Prefer to extend an existing symbol
							if(last_was_pad)
								cap->m_durations[ilast] = end - cap->m_offsets[outlen-1];

							//Nope, need to make a new symbol
							else
							{
								//If we had a gap from a COM character, stretch rearwards into it
								int64_t start = off;
								if(outlen)
									start = cap->m_offsets[ilast] + cap->m_durations[ilast];

								cap->m_offsets.push_back(start);
								cap->m_durations.push_back(end - start);
								cap->m_samples.push_back(PCIeLogicalSymbol(PCIeLogicalSymbol::TYPE_PAD));
							}

							in_packet = false;
						}
						break;

					//K29.7 END
					case 0xfd:
						cap->m_offsets.push_back(off);
						cap->m_durations.push_back(dur);
						cap->m_samples.push_back(PCIeLogicalSymbol(PCIeLogicalSymbol::TYPE_END));
						in_packet = false;
						break;

				}
			}

			//Upper layer payload
			else
			{
				//D10.2 is end of EIEOS
				if( last_was_eie && (sym.m_data == 0x4a) )
					cap->m_durations[ilast] = end - cap->m_offsets[outlen-1];

				//Payload data
				else if(in_packet)
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeLogicalSymbol(
						PCIeLogicalSymbol::TYPE_PAYLOAD_DATA, sym.m_data ^ scrambler_out));
				}

				//Logical idle
				else if( (sym.m_data ^ scrambler_out) == 0)
				{
					//Prefer to extend an existing symbol
					if(last_was_idle)
						cap->m_durations[ilast] = end - cap->m_offsets[ilast];

					//Nope, need to make a new symbol
					else
					{
						cap->m_offsets.push_back(off);
						cap->m_durations.push_back(dur);
						cap->m_samples.push_back(PCIeLogicalSymbol(PCIeLogicalSymbol::TYPE_LOGICAL_IDLE));
					}
				}

				//Garbage: data not inside packet framing
				else
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(dur);
					cap->m_samples.push_back(PCIeLogicalSymbol(PCIeLogicalSymbol::TYPE_ERROR));
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
	cap->MarkModifiedFromCpu();
}

uint8_t PCIeGen2LogicalDecoder::RunScrambler(uint16_t& state)
{
	uint8_t ret = 0;

	for(int j=0; j<8; j++)
	{
		bool b = (state & 0x8000) ? true : false;
		ret >>= 1;

		if(b)
		{
			ret |= 0x80;
			state ^= 0x1c;
		}
		state = (state << 1) | b;
	}

	return ret;
}

std::string PCIeLogicalWaveform::GetColor(size_t i)
{
	const PCIeLogicalSymbol& s = m_samples[i];

	switch(s.m_type)
	{
		case PCIeLogicalSymbol::TYPE_NO_SCRAMBLER:
		case PCIeLogicalSymbol::TYPE_LOGICAL_IDLE:
		case PCIeLogicalSymbol::TYPE_SKIP:
		case PCIeLogicalSymbol::TYPE_PAD:
			return StandardColors::colors[StandardColors::COLOR_IDLE];

		case PCIeLogicalSymbol::TYPE_START_TLP:
		case PCIeLogicalSymbol::TYPE_START_DLLP:
		case PCIeLogicalSymbol::TYPE_END:
		case PCIeLogicalSymbol::TYPE_END_DATA_STREAM:
		case PCIeLogicalSymbol::TYPE_TS1:
		case PCIeLogicalSymbol::TYPE_TS2:
		case PCIeLogicalSymbol::TYPE_IDLE:
		case PCIeLogicalSymbol::TYPE_EXIT_IDLE:
			return StandardColors::colors[StandardColors::COLOR_CONTROL];

		case PCIeLogicalSymbol::TYPE_PAYLOAD_DATA:
			return StandardColors::colors[StandardColors::COLOR_DATA];

		case PCIeLogicalSymbol::TYPE_END_BAD:
		case PCIeLogicalSymbol::TYPE_ERROR:
		default:
			return StandardColors::colors[StandardColors::COLOR_ERROR];
	}
}

string PCIeLogicalWaveform::GetText(size_t i)
{
	char tmp[16];

	const PCIeLogicalSymbol& s = m_samples[i];

	switch(s.m_type)
	{
		case PCIeLogicalSymbol::TYPE_NO_SCRAMBLER:
			return "Scrambler desynced";

		case PCIeLogicalSymbol::TYPE_LOGICAL_IDLE:
			return "Logical Idle";

		case PCIeLogicalSymbol::TYPE_SKIP:
			return "Skip";

		case PCIeLogicalSymbol::TYPE_PAD:
			return "Pad";

		case PCIeLogicalSymbol::TYPE_TS1:
			return "TS1";
		case PCIeLogicalSymbol::TYPE_TS2:
			return "TS2";

		case PCIeLogicalSymbol::TYPE_IDLE:
			return "Electrical Idle";

		case PCIeLogicalSymbol::TYPE_EXIT_IDLE:
			return "Exit Electrical Idle";

		case PCIeLogicalSymbol::TYPE_START_TLP:
			return "TLP";

		case PCIeLogicalSymbol::TYPE_START_DLLP:
			return "DLLP";

		case PCIeLogicalSymbol::TYPE_END:
			return "End";

		case PCIeLogicalSymbol::TYPE_PAYLOAD_DATA:
			snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
			return tmp;

		case PCIeLogicalSymbol::TYPE_END_BAD:
			return "End Bad";

		case PCIeLogicalSymbol::TYPE_END_DATA_STREAM:
			return "End Data Stream";

		case PCIeLogicalSymbol::TYPE_ERROR:
		default:
			return "ERROR";
	}
}
