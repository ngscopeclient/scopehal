
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
#include "DAC8552Decoder.h"
#include "SPIDecoder.h"

using namespace std;

DAC8552Decoder::DAC8552Decoder(const string& color)
	: Filter(color, CAT_MISC)
{
	AddProtocolStream("data");
	CreateInput<InputConstraintWaveformType<SPIWaveform>>("spi");
}

string DAC8552Waveform::GetColor(size_t)
{
	return m_color;
}

string DAC8552Waveform::GetText(size_t i)
{
	const DAC8552Symbol& s = m_samples[i];

	char tmp[128];
	snprintf(tmp, sizeof(tmp), "Load %s %s, Bfr=%c, Value=%d",
			s.loadA() ? "A" : "", s.loadB() ? "B" : "", s.bfrSelect() ? 'A' : 'B', s.m_value);
	return string(tmp);
}

string DAC8552Decoder::GetProtocolName()
{
	return "DAC8552";
}

void DAC8552Decoder::Refresh(
		[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
		[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	ClearMessages();

	if(!VerifyAllInputsOK())
	{
		if(!GetInput(0))
			AddErrorMessage("Missing inputs", "No signal input connected");
		else if(!GetInputWaveform(0))
			AddErrorMessage("Missing inputs", "No waveform available at input");
		SetData(nullptr, 0);
		return;
	}

	auto din = dynamic_cast<SPIWaveform*>(GetInputWaveform(0));
	if(!din)
	{
		AddErrorMessage("Missing inputs", "Invalid input connected");
		SetData(nullptr, 0);
		return;
	}
	size_t len = din->m_samples.size();

	auto cap = new DAC8552Waveform(m_displaycolor);
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	cap->PrepareForCpuAccess();
	din->PrepareForCpuAccess();
	DAC8552Symbol samp;
	int state = 0;
	int64_t offset = 0;

	for(size_t i=0; i<len; i++) {
		auto s = din->m_samples[i];

		switch(state)
		{
			case 0:
				if(s.m_stype == SPISymbol::TYPE_SELECT)
					state = 1;
				break;
			case 1:
				if(s.m_stype == SPISymbol::TYPE_DATA)
				{
					offset = din->m_offsets[i];
					samp.m_flags = s.m_data & (DAC8552Symbol::MASK_LOAD_A | DAC8552Symbol::MASK_LOAD_B | DAC8552Symbol::MASK_BFR_SEL);
					state = 2;
				} else
					state = 0;
				break;
			case 2:
				if(s.m_stype == SPISymbol::TYPE_DATA)
				{
					samp.m_value = static_cast<uint16_t>(s.m_data) << 8;
					state = 3;
				} else
					state = 0;
				break;
			case 3:
				if(s.m_stype == SPISymbol::TYPE_DATA)
				{
					samp.m_value |= s.m_data;
					cap->m_offsets.push_back(offset);
					cap->m_durations.push_back(din->m_offsets[i] + din->m_durations[i] - offset);
					cap->m_samples.push_back(samp);
					state = 4;
				} else
					state = 0;
				break;
			case 4:
				if(s.m_stype == SPISymbol::TYPE_DESELECT)
					state = 0;
				break;
		}
	}
	cap->MarkSamplesModifiedFromCpu();
	cap->MarkTimestampsModifiedFromCpu();
	SetData(cap, 0);
}
