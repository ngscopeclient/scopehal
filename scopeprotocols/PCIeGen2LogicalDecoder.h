/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2023 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Declaration of PCIe2Gen2LogicalDecoder
 */

#ifndef PCIe2Gen2LogicalDecoder_h
#define PCIe2Gen2LogicalDecoder_h

class PCIeLogicalSymbol
{
public:

	enum SymbolType
	{
		TYPE_NO_SCRAMBLER,		//unknown data before the scrambler seed is figured out
		TYPE_LOGICAL_IDLE,		//nothing happening
		TYPE_SKIP,				//rate matching character
		TYPE_START_TLP,			//Begin an upper layer packet
		TYPE_START_DLLP,
		TYPE_END,				//End a TLP or DLLP
		TYPE_END_BAD,			//End a packet, but mark it as to be ignored
		TYPE_PAYLOAD_DATA,		//A byte of TLP or DLLP data
		TYPE_END_DATA_STREAM,	//End of a data stream
		TYPE_ERROR,
		TYPE_TS1,				//Training set
		TYPE_TS2,
		TYPE_IDLE,
		TYPE_EXIT_IDLE,
		TYPE_PAD
	} m_type;

	uint8_t m_data;

	PCIeLogicalSymbol()
	{}

	PCIeLogicalSymbol(SymbolType type, uint8_t data = 0)
		: m_type(type)
		, m_data(data)
	{}


	bool operator==(const PCIeLogicalSymbol& s) const
	{
		return (m_type == s.m_type) && (m_data == s.m_data);
	}
};

class PCIeLogicalWaveform : public SparseWaveform<PCIeLogicalSymbol>
{
public:
	PCIeLogicalWaveform () : SparseWaveform<PCIeLogicalSymbol>() {};
	virtual std::string GetText(size_t) override;
	virtual std::string GetColor(size_t) override;
};

/**
	@brief Decoder for PCIe gen 1/2 logical sub-block
 */
class PCIeGen2LogicalDecoder : public Filter
{
public:
	PCIeGen2LogicalDecoder(const std::string& color);
	virtual ~PCIeGen2LogicalDecoder();

	virtual void Refresh() override;

	static std::string GetProtocolName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	PROTOCOL_DECODER_INITPROC(PCIeGen2LogicalDecoder)

protected:
	uint8_t RunScrambler(uint16_t& state);

	void RefreshPorts();

	std::string m_portCountName;
};

#endif
