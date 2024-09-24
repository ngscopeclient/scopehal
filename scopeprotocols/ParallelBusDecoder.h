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
	@author Federic BORRY
	@brief Declaration of ParallelBusDecoder
 */
#ifndef ParallelBusDecoder_h
#define ParallelBusDecoder_h

#include "../scopehal/PacketDecoder.h"

class ParallelBus8BitsWaveform : public SparseWaveform<uint8_t>
{
public:
	ParallelBus8BitsWaveform (const std::string& color) : SparseWaveform<uint8_t>(), m_color(color) {};
	virtual std::string GetText(size_t) override;
	virtual std::string GetColor(size_t) override;

private:
	const std::string& m_color;
};

class ParallelBus16BitsWaveform : public SparseWaveform<uint16_t>
{
public:
	ParallelBus16BitsWaveform (const std::string& color) : SparseWaveform<uint16_t>(), m_color(color) {};
	virtual std::string GetText(size_t) override;
	virtual std::string GetColor(size_t) override;

private:
	const std::string& m_color;
};

class ParallelBus32BitsWaveform : public SparseWaveform<uint32_t>
{
public:
	ParallelBus32BitsWaveform (const std::string& color) : SparseWaveform<uint32_t>(), m_color(color) {};
	virtual std::string GetText(size_t) override;
	virtual std::string GetColor(size_t) override;

private:
	const std::string& m_color;
};

class ParallelBus64BitsWaveform : public SparseWaveform<uint64_t>
{
public:
	ParallelBus64BitsWaveform (const std::string& color) : SparseWaveform<uint64_t>(), m_color(color) {};
	virtual std::string GetText(size_t) override;
	virtual std::string GetColor(size_t) override;

private:
	const std::string& m_color;
};


class ParallelBusDecoder : public PacketDecoder
{
public:
	ParallelBusDecoder(const std::string& color);

	virtual void Refresh() override;

	static std::string GetProtocolName();

	virtual std::vector<std::string> GetHeaders() override;

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	PROTOCOL_DECODER_INITPROC(ParallelBusDecoder)

	enum ParallelBusWidth {
		WIDTH_8BITS,
		WIDTH_16BITS,
		WIDTH_32BITS,
		WIDTH_64BITS
	};

protected:
	void FinishPacket(Packet* pack);
	std::string m_widthname;
	uint8_t m_width;
	uint8_t m_inputCount;
	void updateWidth();
};

#endif
