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
	@brief Declaration of USB2PMADecoder
 */
#ifndef USB2PMADecoder_h
#define USB2PMADecoder_h

/**
	@brief A single bit on a USB 1.x/2.x differential bus
 */
class USB2PMASymbol
{
public:

	enum SegmentType
	{
		TYPE_J,
		TYPE_K,
		TYPE_SE0,
		TYPE_SE1
	};

	USB2PMASymbol(SegmentType type = TYPE_SE1)
	 : m_type(type)
	{
	}

	SegmentType m_type;

	bool operator==(const USB2PMASymbol& rhs) const
	{
		return (m_type == rhs.m_type);
	}
};

class USB2PMAWaveform : public SparseWaveform<USB2PMASymbol>
{
public:
	USB2PMAWaveform () : SparseWaveform<USB2PMASymbol>() {};
	virtual std::string GetText(size_t) override;
	virtual std::string GetColor(size_t) override;
};

class USB2PMADecoder : public Filter
{
public:
	USB2PMADecoder(const std::string& color);

	virtual void Refresh() override;

	static std::string GetProtocolName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	enum Speed
	{
		SPEED_LOW,
		SPEED_FULL,
		SPEED_HIGH
	};

	void SetSpeed(Speed s)
	{ m_parameters[m_speedname].SetIntVal(s); }

	PROTOCOL_DECODER_INITPROC(USB2PMADecoder)

protected:
	std::string m_speedname;
};

#endif
