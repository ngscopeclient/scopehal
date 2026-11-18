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
	@brief Implementation of Ethernet1000BaseXRawDecoder
 */
#include "../scopehal/scopehal.h"
#include "../scopehal/Filter.h"
#include "IBM8b10bDecoder.h"
#include "Ethernet1000BaseXRawDecoder.h"


using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

Ethernet1000BaseXRawDecoder::Ethernet1000BaseXRawDecoder(const string& color)
	: PacketDecoder(color, CAT_SERIAL)
{
	//Add inputs. We take a single 8b10b coded stream
	CreateInput<InputConstraintWaveformType<IBM8b10bWaveform> >("data");

	AddProtocolStream("data");
}

Ethernet1000BaseXRawDecoder::~Ethernet1000BaseXRawDecoder()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

string Ethernet1000BaseXRawDecoder::GetProtocolName()
{
	return "Ethernet - 1000BaseX Raw";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void Ethernet1000BaseXRawDecoder::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("Ethernet1000BaseXRawDecoder::Refresh");
	#endif
	ClearMessages();

	if(!VerifyAllInputsOK())
	{
		AddErrorMessage("Missing input", "One or more inputs are unconnected");
		SetData(nullptr, 0);
		return;
	}

	//Create the capture
	//Output is time aligned with the input
	auto data = dynamic_cast<IBM8b10bWaveform*>(GetInputWaveform(0));
	auto cap = SetupEmptyWaveform<Ethernet1000BaseXRawWaveform>(data, 0);
	cap->m_timescale = 1;
	cap->m_triggerPhase = 0;
	cap->PrepareForCpuAccess();

	Packet* pack = nullptr;

	size_t len = data->m_samples.size();
	for(size_t i=0; i < len; i++)
	{
		//Ignore idles and autonegotiation for now

		auto symbol = data->m_samples[i];

		//TODO: more efficient packet formatting to skip this unnecessary scaling
		auto off = data->m_offsets[i] * data->m_timescale;
		auto dur = data->m_durations[i] * data->m_timescale;

		//K27.7 is a start-of-frame
		Unit fs(Unit::UNIT_FS);
		if( (symbol.m_flags & IBM8b10bSymbol::FLAG_CONTROL) && (symbol.m_data == 0xfb) )
		{
			cap->m_offsets.push_back(off);
			cap->m_durations.push_back(dur);
			cap->m_samples.push_back(Ethernet1000BaseXRawSymbol(Ethernet1000BaseXRawSymbol::TYPE_START, 0));
		}

		//Discard anything else
		else
			continue;

		//make the packet
		pack = new Packet;
		m_packets.push_back(pack);
		pack->m_offset = off * cap->m_timescale;
		pack->m_len = 0;

		i++;

		//Decode frame data until we see a control or error character.
		//Any control character would mean end-of-frame or error.
		while(i < len)
		{
			off = data->m_offsets[i] * data->m_timescale;
			dur = data->m_durations[i] * data->m_timescale;
			symbol = data->m_samples[i];

			//Expect K29.7 end of frame
			if(symbol.m_flags & IBM8b10bSymbol::FLAG_CONTROL)
			{
				cap->m_offsets.push_back(off);
				cap->m_durations.push_back(dur);
				cap->m_samples.push_back(Ethernet1000BaseXRawSymbol(Ethernet1000BaseXRawSymbol::TYPE_STOP, 0));

				break;
			}

			else
			{
				cap->m_offsets.push_back(off);
				cap->m_durations.push_back(dur);
				cap->m_samples.push_back(Ethernet1000BaseXRawSymbol(Ethernet1000BaseXRawSymbol::TYPE_DATA, symbol.m_data));

				pack->m_data.push_back(symbol.m_data);

				pack->m_len = off + dur - pack->m_offset;
			}

			i++;
		}
	}

	cap->MarkModifiedFromCpu();
}

string Ethernet1000BaseXRawWaveform::GetColor(size_t i)
{
	const Ethernet1000BaseXRawSymbol& s = m_samples[i];

	switch(s.m_type)
	{
		case Ethernet1000BaseXRawSymbol::TYPE_START:
		case Ethernet1000BaseXRawSymbol::TYPE_STOP:
			return StandardColors::colors[StandardColors::COLOR_CONTROL];

		case Ethernet1000BaseXRawSymbol::TYPE_DATA:
			return StandardColors::colors[StandardColors::COLOR_DATA];

		default:
			return StandardColors::colors[StandardColors::COLOR_ERROR];
	}
}

string Ethernet1000BaseXRawWaveform::GetText(size_t i)
{
	char tmp[16];

	const Ethernet1000BaseXRawSymbol& s = m_samples[i];

	switch(s.m_type)
	{
		case Ethernet1000BaseXRawSymbol::TYPE_START:
			return "SOF";
		case Ethernet1000BaseXRawSymbol::TYPE_STOP:
			return "EOF";

		case Ethernet1000BaseXRawSymbol::TYPE_DATA:
			snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
			return tmp;

		//case Ethernet1000BaseXRawSymbol::TYPE_ERROR:
		default:
			return "ERROR";
	}
}

vector<string> Ethernet1000BaseXRawDecoder::GetHeaders()
{
	vector<string> ret;
	return ret;
}
