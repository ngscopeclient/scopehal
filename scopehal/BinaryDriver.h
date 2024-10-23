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
	@author Frederic BORRY
	@brief Helper class for binary driver: provides methods for handling binary data

	@ingroup core
 */

#ifndef BinaryDriver_h
#define BinaryDriver_h

/**
	@brief Helper class for binary driver: provides methods for handling binary data
	@ingroup core
 */
class BinaryDriver
{
public:
	BinaryDriver();
	~BinaryDriver();


protected:

	/**
		@brief Helper function to push a uint16 to a std::vector of bytes, as two consecutive bytes (either little-endian or big-edian according to littleEndian parameter)

		@param data 		the vector to push data to
		@param value 		the value to push
		@param littleEndian push data in little-endian format if true (default), big-endian otherwise

	 */
	void PushUint16(std::vector<uint8_t>* data, uint16_t value, bool littleEndian = true)
	{
		data->push_back(reinterpret_cast<uint8_t *>(&value)[littleEndian ? 0 : 1]);
		data->push_back(reinterpret_cast<uint8_t *>(&value)[littleEndian ? 1 : 0]);
	}

	/**
		@brief Bounds-checked read of a 16-bit value from a byte vector

		@param data			Input vector
		@param index		Byte index to read
		@param littleEndian push data in little-endian format if true (default), big-endian otherwise

		@return			data[index] if in bounds, or 0 if out of bounds
	 */
	uint16_t ReadUint16(const std::vector<uint8_t>& data, uint8_t index, bool littleEndian = true)
	{
		if(data.size() <= ((size_t)(index+1)))
			return 0;
		return (static_cast<uint16_t>(data[littleEndian ? index : (index+1)]) + (static_cast<uint16_t>(data[littleEndian ? (index+1) : index]) << 8));
	}

	/**
		@brief Bounds-checked read of an 8-bit value from a byte vector

		@param data		Input vector
		@param index	Byte index to read

		@return			data[index] if in bounds, or 0 if out of bounds
	 */
	uint8_t ReadUint8(const std::vector<uint8_t>& data, uint8_t index)
	{
		if(data.size() <= ((size_t)(index)))
			return 0;
		return data[index];
	}

	uint16_t CalculateCRC(const uint8_t *buff, size_t len);
};

#endif
