
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
	@author Daniel Bauer
	@brief Declaration of DAC8552Decoder
*/
#ifndef DAC8522Decoder_h
#define DAC8522Decoder_h

class DAC8552Symbol
{
public:
	DAC8552Symbol(uint8_t flags = 0, uint16_t value=0xDEAD)
		: m_flags(flags)
		, m_value(value)
		 {}
	// Rather store all the flags in 1 byte instead of making the struct 5 bytes large and then get funky padding
	// This will most likely get padded to a nice handy integer
	static constexpr uint8_t MASK_LOAD_A = 0x10;
	static constexpr uint8_t MASK_LOAD_B = 0x20;
	static constexpr uint8_t MASK_BFR_SEL = 0x04;
	uint8_t m_flags;
	uint16_t m_value;

	bool operator==(const DAC8552Symbol& s) const
	{
		return (m_flags == s.m_flags) && (m_value == s.m_value);
	}
	bool loadA() const { return m_flags & MASK_LOAD_A ? true : false; }
	bool loadB() const { return m_flags & MASK_LOAD_B ? true : false; }
	bool bfrSelect() const { return m_flags & MASK_BFR_SEL ? true : false; }
};

class DAC8552Waveform : public SparseWaveform<DAC8552Symbol>
{
public:
	DAC8552Waveform (const std::string& color) : SparseWaveform<DAC8552Symbol>(), m_color(color) {};
	virtual std::string GetText(size_t override);
	virtual std::string GetColor(size_t override);

private:
	const std::string& m_color;
};

class DAC8552Decoder : public Filter
{
public:
	DAC8552Decoder(const std::string& color);

	virtual void Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue) override;
	static std::string GetProtocolName();

	PROTOCOL_DECODER_INITPROC(DAC8552Decoder)
};

#endif
