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
	@brief Helper class for command line drivers: provides helper methods for command line based communication with devices like NanoVNA or TinySA

	@ingroup core
 */

#ifndef CommandLineDriver_h
#define CommandLineDriver_h

/**
	@brief Helper class for command line drivers: provides helper methods for command line based communication with devices like NanoVNA or TinySA
	@ingroup core
 */
class CommandLineDriver: public virtual SCPIDevice
{
public:
	CommandLineDriver(SCPITransport* transport);
	~CommandLineDriver();


protected:
	// Make sure several request don't collide before we received the corresponding response
	std::recursive_mutex m_transportMutex;

	/**
	 * @brief Converse with the device : send a command and read the reply over several lines
	 *
	 * @param commandString the command string to send
	 * @param readLines a verctor to store the reply lines
	 * @param hasEcho true (default value) if the device is expected to echo the sent command
	 * @return size_t the number of lines received from the device
	 */
	std::string ConverseSingle(const std::string commandString, bool hasEcho = true);


	/**
	 * @brief Converse with the device by sending a command and receiving a single line response
	 *
	 * @param commandString the command string to send
	 * @return std::string the received response
	 * @param hasEcho true (default value) if the device is expected to echo the sent command
	 * @param progress (optional) download progress function
	 * @param expectedLines (optional) the number of lines expected from the device
	 * @return size_t the number of lines received from the device
	 */
	size_t ConverseMultiple(const std::string commandString, std::vector<std::string> &readLines, bool hasEcho = true, std::function<void(float)> progress = nullptr, size_t expecedLines = 0);

	/**
	 * @brief Set and/or read the sweep values from the device
	 *
	 * @param sweepStart the sweep start value (in/out)
	 * @param sweepStop the sweep stop value (in/out)
	 * @param setValue tru is the values have to be set on the device
	 * @return true is the value returned by the device is different from the one that shoudl have been set (e.g. out of range)
	 */
	bool ConverseSweep(int64_t &sweepStart, int64_t &sweepStop, bool setValue = false);

	/**
	 * @brief Base method to converse with the device
	 *
	 * @param commandString the command string to send to the device
	 * @param progress (optional) download progress function
	 * @param expectedLines (optional) the number of lines expected from the device
	 * @return std::string a string containing all the response from the device (may contain several lines separated by \r \n)
	 */
	std::string ConverseString(const std::string commandString, std::function<void(float)> progress = nullptr, size_t expecedLines = 0);

	/**
	 * @brief Remove CR from the provided line
	 * 
	 * @param toClean the line to remove the CR from (in/out)
	 */
	static void RemoveCR(std::string &toClean)
	{
		toClean.erase( std::remove(toClean.begin(), toClean.end(), '\r'), toClean.end() );
	}

	size_t m_maxResponseSize;
	double m_communicationTimeout;

	// @brief Trailer string expected at the end of a response from the device (command prompt)
	inline static const std::string TRAILER_STRING = "ch> ";
	// @brief Legth of the trailer string expected at the end of a response from the device (command prompt)
	inline static const size_t TRAILER_STRING_LENGTH = TRAILER_STRING.size();
	// @brief End Of Line string
	inline static const std::string EOL_STRING = "\r\n";
	// @brief Size of the EOL string
	inline static const size_t EOL_STRING_LENGTH = EOL_STRING.size();
};
#endif
