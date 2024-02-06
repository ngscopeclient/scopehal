/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2024 Andrew D. Zonenberg and contributors                                                         *
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

#ifndef CANChannel_h
#define CANChannel_h

class CANSymbol
{
public:
	enum stype
	{
		TYPE_SOF,
		TYPE_ID,
		TYPE_RTR,
		TYPE_R0,
		TYPE_FD,
		TYPE_DLC,
		TYPE_DATA,
		TYPE_CRC_OK,
		TYPE_CRC_BAD,
		TYPE_CRC_DELIM,
		TYPE_ACK,
		TYPE_ACK_DELIM,
		TYPE_EOF
	};

	CANSymbol()
	{}

	CANSymbol(stype t, uint32_t data)
	 : m_stype(t)
	 , m_data(data)
	{
	}

	stype m_stype;
	uint32_t m_data;

	bool operator== (const CANSymbol& s) const
	{
		return (m_stype == s.m_stype) && (m_data == s.m_data);
	}
};

class CANWaveform : public SparseWaveform<CANSymbol>
{
public:
	CANWaveform () : SparseWaveform<CANSymbol>() {};
	virtual std::string GetText(size_t) override;
	virtual std::string GetColor(size_t) override;
};

/**
	@brief A single channel of a CAN bus interface
 */
class CANChannel : public OscilloscopeChannel
{
public:

	CANChannel(
		Oscilloscope* scope,
		const std::string& hwname,
		const std::string& color = "#808080",
		size_t index = 0);

	virtual ~CANChannel();
};

#endif
