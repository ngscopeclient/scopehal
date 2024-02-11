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
	@brief Declaration of DPhyDataDecoder
 */
#ifndef DPhyDataDecoder_h
#define DPhyDataDecoder_h

class DPhyDataSymbol
{
public:
	enum SymbolType
	{
		TYPE_SOT,
		TYPE_TURNAROUND_REQUEST,
		TYPE_HS_DATA,
		TYPE_EOT,
		TYPE_ERROR
	};

	DPhyDataSymbol(SymbolType t=DPhyDataSymbol::TYPE_ERROR, uint8_t data=0)
	 : m_type(t)
	 , m_data(data)
	{}

	SymbolType m_type;
	uint8_t m_data;

	bool operator== (const DPhyDataSymbol& s) const
	{
		return (m_type == s.m_type) && (m_data == s.m_data);
	}
};

class DPhyDataWaveform : public SparseWaveform<DPhyDataSymbol>
{
public:
	DPhyDataWaveform () : SparseWaveform<DPhyDataSymbol>() {};
	virtual std::string GetText(size_t) override;
	virtual std::string GetColor(size_t) override;
};

class DPhyDataDecoder : public Filter
{
public:
	DPhyDataDecoder(const std::string& color);

	virtual void Refresh() override;

	static std::string GetProtocolName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	PROTOCOL_DECODER_INITPROC(DPhyDataDecoder)
};

#endif
