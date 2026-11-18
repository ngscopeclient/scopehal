/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Declaration of LogMessageProvider
	@ingroup core
 */
#ifndef LogMessageProvider_h
#define LogMessageProvider_h

/**
	@brief An object that can produce log/error messages
	@ingroup core
**/

class LogMessageProvider
{
public:
	LogMessageProvider()
	{}

	virtual ~LogMessageProvider()
	{}

	///@brief Checks if this graph has any messages
	bool HasMessages() const
	{ return not m_messages.empty(); }

	///@brief Walking through any messages
	const std::vector<LogMessage>& GetMessages() const
	{ return m_messages; }

	const LogMessage* GetMostSevereMessage() const;

protected:

	///@brief Remove any messages left over from the last graph refresh
	void ClearMessages()
	{ m_messages.clear(); }

	///@brief Attach a new message to the node
	void AddMessage(const LogMessage& msg)
	{ m_messages.push_back(msg); }

	///@brief Attach a new message to the node
	void AddMessage(Severity severity, const std::string& title, const std::string& message)
	{ AddMessage(LogMessage(severity, title, message)); }

	///@brief Add a new error message
	void AddErrorMessage(const std::string& err)
	{ AddErrorMessage("Unknown", std::string("• ") + err); }

	/**
		@brief Add a new error message with a title

		The title replaces any previous title, if set
	 */
	void AddErrorMessage(const std::string& title, const std::string& err)
	{ AddMessage(Severity::ERROR, title, err); }

	//@brief All the messages pending display
	std::vector<LogMessage> m_messages;
};

#endif
