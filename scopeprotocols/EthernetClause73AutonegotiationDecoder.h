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
	@author Marcin Dawidowicz
	@brief Declaration of Ethernet Clause 73 Autonegotiation Decoder
*/

#ifndef EthernetClause73AutonegotiationDecoder_h
#define EthernetClause73AutonegotiationDecoder_h

/**
	@brief A single Ethernet Clause 73 autonegotiation code page (49 bits)
 */
struct Clause73CodePage
{
	uint8_t selector_field;            // D[4:0]
	uint8_t echoed_nonce;              // D[9:5]
	bool c0_pause;                     // D[10]
	bool c1_pause;                     // D[11]
	bool c2_reserved;                  // D[12]
	bool rf;                           // D[13]
	bool ack;                          // D[14]
	bool np;                           // D[15]
	uint8_t transmitted_nonce;         // D[20:16]
	uint32_t technology_ability;       // D[43:21]
	uint8_t fec;                       // D[47:44]
	bool code;                         // D[48]
};

class Clause73Waveform : public SparseWaveform<Clause73CodePage>
{
public:
	Clause73Waveform(FilterParameter& displayformat) : SparseWaveform<Clause73CodePage>(), m_displayformat(displayformat) {}
	virtual std::string GetText(size_t) override;
	virtual std::string GetColor(size_t) override;

	FilterParameter& m_displayformat;
};

class EthernetClause73AutonegotiationDecoder : public Filter
{
public:
	EthernetClause73AutonegotiationDecoder(const std::string& color);

	virtual void Refresh() override;

	static std::string GetProtocolName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	static FilterParameter MakeDisplayFormatParameter();

	enum DisplayFormat
	{
		FORMAT_COMPACT,
		FORMAT_DETAILED
	};

	PROTOCOL_DECODER_INITPROC(EthernetClause73AutonegotiationDecoder)

protected:
	std::string m_displayformat;
};

#endif
