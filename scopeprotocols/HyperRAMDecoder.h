/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2021 Andrew D. Zonenberg and contributors                                                         *
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
	@author Mike Walters
	@brief Declaration of HyperRAMDecoder
 */

#ifndef HyperRAMDecoder_h
#define HyperRAMDecoder_h

class HyperRAMSymbol
{
public:
	enum stype
	{
		TYPE_SELECT,
		TYPE_CA,
		TYPE_WAIT,
		TYPE_DATA,
		TYPE_DESELECT,
		TYPE_ERROR
	};

	HyperRAMSymbol()
	{}

	HyperRAMSymbol(stype t, uint64_t d)
	 : m_stype(t)
	 , m_data(d)
	{}

	stype m_stype;
	uint64_t m_data;

	bool operator==(const HyperRAMSymbol& s) const
	{
		return (m_stype == s.m_stype) && (m_data == s.m_data);
	}
};

class HyperRAMWaveform : public Waveform<HyperRAMSymbol>
{
public:
	HyperRAMWaveform () : Waveform<HyperRAMSymbol>() {};
	virtual std::string GetText(size_t) override;
	virtual Gdk::Color GetColor(size_t) override;
};

class HyperRAMDecoder : public Filter
{
public:
	HyperRAMDecoder(const std::string& color);

	void Refresh() override;

	static std::string GetProtocolName();

	bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	PROTOCOL_DECODER_INITPROC(HyperRAMDecoder)

	struct CA
	{
		uint32_t address;
		bool read;
		bool register_space;
		bool linear;
	};
	static struct CA DecodeCA(uint64_t data);

protected:
	std::string m_latencyname;
};

#endif
