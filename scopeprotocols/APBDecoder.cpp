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
#include "APBDecoder.h"
#include "SWDDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

APBDecoder::APBDecoder(const string& color)
	: PacketDecoder(color, CAT_BUS)
{
	CreateInput<InputConstraintAND>(
		"penable",
		initializer_list<shared_ptr<InputConstraint> >
		{
			make_shared<InputConstraintXUnit>(this, Unit(Unit::UNIT_FS)),
			make_shared<InputConstraintStreamType>(this, Stream::STREAM_TYPE_DIGITAL)
		});

	CreateInput<InputConstraintAND>(
		"psel",
		initializer_list<shared_ptr<InputConstraint> >
		{
			make_shared<InputConstraintXUnit>(this, Unit(Unit::UNIT_FS)),
			make_shared<InputConstraintStreamType>(this, Stream::STREAM_TYPE_DIGITAL)
		});

	CreateInput<InputConstraintAND>(
		"pready",
		initializer_list<shared_ptr<InputConstraint> >
		{
			make_shared<InputConstraintXUnit>(this, Unit(Unit::UNIT_FS)),
			make_shared<InputConstraintStreamType>(this, Stream::STREAM_TYPE_DIGITAL)
		});

	CreateInput<InputConstraintAND>(
		"pwrite",
		initializer_list<shared_ptr<InputConstraint> >
		{
			make_shared<InputConstraintXUnit>(this, Unit(Unit::UNIT_FS)),
			make_shared<InputConstraintStreamType>(this, Stream::STREAM_TYPE_DIGITAL)
		});

	CreateInput<InputConstraintAND>(
		"pslverr",
		initializer_list<shared_ptr<InputConstraint> >
		{
			make_shared<InputConstraintXUnit>(this, Unit(Unit::UNIT_FS)),
			make_shared<InputConstraintStreamType>(this, Stream::STREAM_TYPE_DIGITAL)
		});

	CreateInput<InputConstraintAND>(
		"paddr",
		initializer_list<shared_ptr<InputConstraint> >
		{
			make_shared<InputConstraintXUnit>(this, Unit(Unit::UNIT_FS)),
			make_shared<InputConstraintStreamType>(this, Stream::STREAM_TYPE_DIGITAL_BUS)
		});

	CreateInput<InputConstraintAND>(
		"pwdata",
		initializer_list<shared_ptr<InputConstraint> >
		{
			make_shared<InputConstraintXUnit>(this, Unit(Unit::UNIT_FS)),
			make_shared<InputConstraintStreamType>(this, Stream::STREAM_TYPE_DIGITAL_BUS)
		});

	CreateInput<InputConstraintAND>(
		"prdata",
		initializer_list<shared_ptr<InputConstraint> >
		{
			make_shared<InputConstraintXUnit>(this, Unit(Unit::UNIT_FS)),
			make_shared<InputConstraintStreamType>(this, Stream::STREAM_TYPE_DIGITAL_BUS)
		});

	CreateInput<InputConstraintAND>(
		"pstrb",
		initializer_list<shared_ptr<InputConstraint> >
		{
			make_shared<InputConstraintXUnit>(this, Unit(Unit::UNIT_FS)),
			make_shared<InputConstraintStreamType>(this, Stream::STREAM_TYPE_DIGITAL_BUS)
		});

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

vector<string> APBDecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("Op");
	ret.push_back("Address");
	return ret;
}

string APBDecoder::GetProtocolName()
{
	return "APB";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void APBDecoder::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue
	)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("APBDecoder::Refresh");
	#endif
	ClearMessages();
	ClearPackets();

	//Get the inputs
	//For now, assume uniform
	auto penable = dynamic_cast<UniformDigitalWaveform*>(GetInputWaveform(0));
	auto psel = dynamic_cast<UniformDigitalWaveform*>(GetInputWaveform(1));
	auto pready = dynamic_cast<UniformDigitalWaveform*>(GetInputWaveform(2));
	auto pwrite = dynamic_cast<UniformDigitalWaveform*>(GetInputWaveform(3));
	auto pslverr = dynamic_cast<UniformDigitalWaveform*>(GetInputWaveform(4));
	auto paddr = dynamic_cast<UniformDigitalBusWaveform32*>(GetInputWaveform(5));	//TODO support 64 bit
	auto pwdata = dynamic_cast<UniformDigitalBusWaveform32*>(GetInputWaveform(6));	//TODO support 64 bit
	auto prdata = dynamic_cast<UniformDigitalBusWaveform32*>(GetInputWaveform(7));	//TODO support 64 bit
	auto pstrb = dynamic_cast<UniformDigitalBusWaveform32*>(GetInputWaveform(8));

	//PSLVERR and PSTRB are optional, the bus is decodeable without them
	//(in fact, we don't even use them yet)
	//Everything else is mandatory for the decode to be possible
	if(!penable || !psel || !pready || !pwrite || !paddr || !pwdata || !prdata)
	{
		if(!penable)
			AddErrorMessage("Missing input", "Required input PENABLE is unconnected or has no waveform");
		if(!psel)
			AddErrorMessage("Missing input", "Required input PSEL is unconnected or has no waveform");
		if(!pready)
			AddErrorMessage("Missing input", "Required input PREADY is unconnected or has no waveform");
		if(!pwrite)
			AddErrorMessage("Missing input", "Required input PWRITE is unconnected or has no waveform");
		if(!paddr)
			AddErrorMessage("Missing input", "Required input PADDR is unconnected or has no waveform");
		if(!pwdata)
			AddErrorMessage("Missing input", "Required input PWDATA is unconnected or has no waveform");
		if(!prdata)
			AddErrorMessage("Missing input", "Required input PRDATA is unconnected or has no waveform");

		SetData(nullptr, 0);
		return;
	}

	//Prep the inputs
	penable->PrepareForCpuAccess();
	psel->PrepareForCpuAccess();
	pready->PrepareForCpuAccess();
	pwrite->PrepareForCpuAccess();
	if(pslverr)
		pslverr->PrepareForCpuAccess();
	paddr->PrepareForCpuAccess();
	pwdata->PrepareForCpuAccess();
	prdata->PrepareForCpuAccess();
	if(pstrb)
		pstrb->PrepareForCpuAccess();

	//Set up output
	auto cap = SetupEmptyWaveform<APBWaveform>(penable, 0);
	cap->PrepareForCpuAccess();

	//Get data length
	size_t len = penable->size();
	len = min(len, psel->size());
	len = min(len, pready->size());
	len = min(len, pwrite->size());
	len = min(len, paddr->size());
	len = min(len, pwdata->size());
	len = min(len, prdata->size());

	//Main decode loop
	//For now, use implicit clock
	enum
	{
		STATE_IDLE,
		STATE_SELECTED
	} state = STATE_IDLE;
	int64_t tstart = 0;

	uint32_t addrNibbles = ceil(1.0 * GetInput(0).GetDigitalWidth() / 4);
	uint32_t dataWidth = GetInput(7).GetDigitalWidth();
	uint32_t dataBytes = ceil(1.0 * dataWidth / 8);

	for(size_t i=0; i<len; i++)
	{
		switch(state)
		{
			//Wait for PSEL to go high
			case STATE_IDLE:

				//If we have a rising edge of PSEL, note the start time
				if(psel->m_samples[i])
				{
					tstart = i;
					state = STATE_SELECTED;
				}

				break;

			//Wait for PREADY and PSEL to go high
			case STATE_SELECTED:

				//If not selected, reset
				if(!psel->m_samples[i])
				{
					state = STATE_IDLE;
					continue;
				}

				//If ready and enabled, this is the final cycle of the transaction
				if(pready->m_samples[i] && penable->m_samples[i])
				{
					auto plen = (i+1) - tstart;
					auto pack = new Packet;
					pack->m_offset = (tstart * penable->m_timescale) + penable->m_triggerPhase;

					cap->m_offsets.push_back(tstart);
					cap->m_durations.push_back(plen);
					pack->m_len = plen * penable->m_timescale;

					auto addr = paddr->m_samples[i];
					pack->m_headers["Address"] = to_string_hex(addr, true, addrNibbles);

					uint64_t data = 0;
					if(pwrite->m_samples[i])
					{
						data = pwdata->m_samples[i];
						pack->m_headers["Op"] = "Write";
						pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
						cap->m_samples.push_back(APBSymbol(true, addr, data));
					}
					else
					{
						data = prdata->m_samples[i];
						pack->m_headers["Op"] = "Read";
						pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
						cap->m_samples.push_back(APBSymbol(false, addr, data));
					}

					//TODO: handle PSLVERR

					//Add packet data
					for(size_t j=0; j<dataBytes; j++)
						pack->m_data.push_back( (data >> (8*j)) & 0xff);

					m_packets.push_back(pack);

					state = STATE_IDLE;
				}

				break;

			default:
				break;
		}
	}

	cap->MarkModifiedFromCpu();
}

string APBWaveform::GetColor(size_t /*i*/)
{
	return StandardColors::colors[StandardColors::COLOR_DATA];
}

string APBWaveform::GetText(size_t i)
{
	char tmp[128] = "";
	const APBSymbol& s = m_samples[i];

	if(s.m_write)
		snprintf(tmp, sizeof(tmp), "Write %08x: %08x", s.m_addr, s.m_data);
	else
		snprintf(tmp, sizeof(tmp), "Read %08x: %08x", s.m_addr, s.m_data);

	return string(tmp);
}
