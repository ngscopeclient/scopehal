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

#include "../scopehal/scopehal.h"
#include "ADL5205Decoder.h"
#include "SPIDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ADL5205Decoder::ADL5205Decoder(const string& color)
	: Filter(color, CAT_MISC)
{
	AddProtocolStream("data");
	CreateInput("spi");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool ADL5205Decoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (dynamic_cast<SPIWaveform*>(stream.m_channel->GetData(0)) != NULL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string ADL5205Decoder::GetProtocolName()
{
	return "ADL5205";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void ADL5205Decoder::Refresh()
{
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	auto din = dynamic_cast<SPIWaveform*>(GetInputWaveform(0));
	if(!din)
	{
		SetData(NULL, 0);
		return;
	}
	size_t len = din->m_samples.size();

	//Loop over the SPI events and process stuff
	auto cap = new ADL5205Waveform(m_displaycolor);
	cap->m_timescale = din->m_timescale;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	cap->PrepareForCpuAccess();
	din->PrepareForCpuAccess();
	ADL5205Symbol samp;
	int phase = 0;
	int64_t offset = 0;
	for(size_t i=0; i<len; i++)
	{
		auto s = din->m_samples[i];

		switch(phase)
		{
			//Wait for us to be selected, ignore any traffic before that
			case 0:
				if(s.m_stype == SPISymbol::TYPE_SELECT)
					phase = 1;
				break;

			//First byte
			case 1:
				if(s.m_stype == SPISymbol::TYPE_DATA)
				{
					offset = din->m_offsets[i];

					if(s.m_data & 1)
						samp.m_write  = false;
					else
						samp.m_write  = true;
					phase = 2;
				}
				else
					phase = 0;
				break;

			//Second byte
			case 2:
				if(s.m_stype == SPISymbol::TYPE_DATA)
				{
					int fa_code = s.m_data >> 6;
					int gain_code = s.m_data & 0x3f;

					//Fast attack
					samp.m_fa = 1 << fa_code;

					//Gain
					if(gain_code > 35)
						gain_code = 35;
					samp.m_gain = 26 - gain_code;

					cap->m_offsets.push_back(offset);
					cap->m_durations.push_back(din->m_offsets[i] + din->m_durations[i] - offset);
					cap->m_samples.push_back(samp);
					phase = 3;
				}
				else
					phase = 0;
				break;

			case 3:
				if(s.m_stype == SPISymbol::TYPE_DESELECT)
					phase = 0;
				break;
		}
	}

	cap->MarkSamplesModifiedFromCpu();
	cap->MarkTimestampsModifiedFromCpu();

	SetData(cap, 0);
}

string ADL5205Waveform::GetColor(size_t /*i*/)
{
	return m_color;
}

string ADL5205Waveform::GetText(size_t i)
{
	const ADL5205Symbol& s = m_samples[i];

	char tmp[128];
	snprintf(tmp, sizeof(tmp), "%s: FA=%d dB, gain=%d dB",
		s.m_write ? "write" : "read", s.m_fa, s.m_gain);
	return string(tmp);
}

