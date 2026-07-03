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

#include "../scopehal/scopehal.h"
#include "IBM8b10bDecoder.h"
#include "EthernetProtocolDecoder.h"
#include "Ethernet1000BaseXDecoder.h"
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

Ethernet1000BaseXDecoder::Ethernet1000BaseXDecoder(const string& color)
	: EthernetProtocolDecoder(color)
{
	//Digital inputs, so need to undo some stuff for the PHY layer decodes
	m_inputs.clear();

	//Add inputs. We take a single 8b10b coded stream
	CreateInput<InputConstraintWaveformType<IBM8b10bWaveform> >("data");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string Ethernet1000BaseXDecoder::GetProtocolName()
{
	return "Ethernet - 1000BaseX";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void Ethernet1000BaseXDecoder::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("Ethernet1000BaseXDecoder::Refresh");
	#endif

	ClearPackets();

	//Make sure we've got valid inputs
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

	auto data = dynamic_cast<IBM8b10bWaveform*>(GetInputWaveform(0));
	data->PrepareForCpuAccess();

	//Create the output capture
	auto cap = SetupEmptyWaveform<EthernetWaveform>(data, 0);
	cap->PrepareForCpuAccess();
	cap->m_timescale = 1;

	size_t len = data->m_samples.size();
	for(size_t i=0; i < len; i++)
	{
		//Ignore idles and autonegotiation for now

		auto symbol = data->m_samples[i];

		//Set of recovered bytes and timestamps
		vector<uint8_t> bytes;
		vector<uint64_t> starts;
		vector<uint64_t> ends;

		//TODO: more efficient packet formatting to skip this unnecessary scaling
		auto off = data->m_offsets[i] * data->m_timescale;
		auto dur = data->m_durations[i] * data->m_timescale;

		//K27.7 is a start-of-frame
		Unit fs(Unit::UNIT_FS);
		if( (symbol.m_flags & IBM8b10bSymbol::FLAG_CONTROL) && (symbol.m_data == 0xfb) )
		{
			bytes.push_back(0x55);
			starts.push_back(off);
			ends.push_back(off + dur);
		}

		//Discard anything else
		else
			continue;

		i++;

		//Decode frame data until we see a control or error character.
		//Any control character would mean end-of-frame or error.
		bool error = false;
		while(i < len)
		{
			off = data->m_offsets[i] * data->m_timescale;
			dur = data->m_durations[i] * data->m_timescale;

			symbol = data->m_samples[i];

			//Expect K29.7 end of frame
			if(symbol.m_flags & IBM8b10bSymbol::FLAG_CONTROL)
			{
				if(symbol.m_data != 0xfd)
					error = true;
				break;
			}

			bytes.push_back(symbol.m_data);
			starts.push_back(off);
			ends.push_back(off + dur);
			i++;
		}

		//TODO: if error, create a single giant "ERROR" frame block? or what

		//Crunch the data
		if(!error)
			BytesToFrames(bytes, starts, ends, cap);
	}

	cap->MarkModifiedFromCpu();
}
