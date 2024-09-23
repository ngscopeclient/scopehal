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

/**
	@file
	@brief Declaration of CANChannel, CANSymbol, and CANWaveform
	@ingroup datamodel
 */

#ifndef CANChannel_h
#define CANChannel_h

/**
	@brief A single symbol within a CAN bus protocol decode
	@ingroup datamodel
 */
class CANSymbol
{
public:

	///@brief Type of the symbol
	enum stype
	{
		///@brief Start of frame
		TYPE_SOF,

		///@brief CAN ID
		TYPE_ID,

		///@brief Remote transmission request bit
		TYPE_RTR,

		///@brief Reserved bit
		TYPE_R0,

		///@brief Full-duplex bit
		TYPE_FD,

		///@brief Data length code
		TYPE_DLC,

		///@brief A data byte
		TYPE_DATA,

		///@brief CRC with a correct value
		TYPE_CRC_OK,

		///@brief CRC with an incorrect value
		TYPE_CRC_BAD,

		///@brief CRC delimiter
		TYPE_CRC_DELIM,

		///@brief Acknowledgement bit
		TYPE_ACK,

		///@brief ACK delimiter
		TYPE_ACK_DELIM,

		///@brief End of frame
		TYPE_EOF
	};

	///@brief Default constructor, performs no initialization
	CANSymbol()
	{}

	/**
		@brief Initializes a CAN symbol

		@param t	Type of the symbol
		@param data	Data value
	 */
	CANSymbol(stype t, uint32_t data)
	 : m_stype(t)
	 , m_data(data)
	{
	}

	///@brief Type of the symbol
	stype m_stype;

	///@brief Data value (meaning depends on type)
	uint32_t m_data;

	/**
		@brief Checks this symbol for equality against a second

		Two symbols are considered equal if both the type and data are equal.

		@param s	The other symbol
	 */
	bool operator== (const CANSymbol& s) const
	{
		return (m_stype == s.m_stype) && (m_data == s.m_data);
	}
};

/**
	@brief A waveform containing CAN bus packets
	@ingroup datamodel
 */
class CANWaveform : public SparseWaveform<CANSymbol>
{
public:
	CANWaveform () : SparseWaveform<CANSymbol>() {};
	virtual std::string GetText(size_t) override;
	virtual std::string GetColor(size_t) override;
};

/**
	@brief A filter or protocol analyzer channel which provides CAN bus data
	@ingroup core
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
