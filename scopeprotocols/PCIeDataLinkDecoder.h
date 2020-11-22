/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2020 Andrew D. Zonenberg                                                                          *
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
	@brief Declaration of PCIeDataLinkDecoder
 */

#ifndef PCIeDataLinkDecoder_h
#define PCIeDataLinkDecoder_h

class PCIeDataLinkSymbol
{
public:

	enum SymbolType
	{
		TYPE_DLLP_TYPE,
		TYPE_DLLP_DATA1,
		TYPE_DLLP_DATA2,
		TYPE_DLLP_DATA3,
		TYPE_DLLP_CRC_OK,
		TYPE_DLLP_CRC_BAD,

		TYPE_ERROR
	} m_type;

	enum DLLPType
	{
		DLLP_TYPE_ACK 							= 0x00,
		DLLP_TYPE_NAK							= 0x10,
		DLLP_TYPE_PM_ENTER_L1					= 0x20,
		DLLP_TYPE_PM_ENTER_L23					= 0x21,
		DLLP_TYPE_PM_ACTIVE_STATE_REQUEST_L1	= 0x23,
		DLLP_TYPE_PM_REQUEST_ACK				= 0x24,
		DLLP_TYPE_VENDOR_SPECIFIC				= 0x30
	};

	uint16_t m_data;

	PCIeDataLinkSymbol()
	{}

	PCIeDataLinkSymbol(SymbolType type, uint8_t data = 0)
		: m_type(type)
		, m_data(data)
	{}


	bool operator==(const PCIeDataLinkSymbol& s) const
	{
		return (m_type == s.m_type) && (m_data == s.m_data);
	}
};

typedef Waveform<PCIeDataLinkSymbol> PCIeDataLinkWaveform;

/**
	@brief Decoder for PCIe data link layer
 */
class PCIeDataLinkDecoder : public Filter
{
public:
	PCIeDataLinkDecoder(const std::string& color);
	virtual ~PCIeDataLinkDecoder();

	virtual std::string GetText(int i);
	virtual Gdk::Color GetColor(int i);

	virtual void Refresh();
	virtual bool NeedsConfig();

	static std::string GetProtocolName();
	virtual void SetDefaultName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream);

	PROTOCOL_DECODER_INITPROC(PCIeDataLinkDecoder)

protected:
	uint16_t CalculateDllpCRC(uint8_t type, uint8_t* data);
};

#endif
