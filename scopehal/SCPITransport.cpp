/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
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
	@brief Implementation of SCPITransport
 */

#include "scopehal.h"

using namespace std;

SCPITransport::CreateMapType SCPITransport::m_createprocs;

SCPITransport::SCPITransport()
	: m_rateLimitingEnabled(false)
	, m_rateLimitingInterval(0)
{
}

SCPITransport::~SCPITransport()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Enumeration

void SCPITransport::DoAddTransportClass(string name, CreateProcType proc)
{
	m_createprocs[name] = proc;
}

void SCPITransport::EnumTransports(vector<string>& names)
{
	for(CreateMapType::iterator it=m_createprocs.begin(); it != m_createprocs.end(); ++it)
		names.push_back(it->first);
}

SCPITransport* SCPITransport::CreateTransport(const string& transport, const string& args)
{
	if(m_createprocs.find(transport) != m_createprocs.end())
		return m_createprocs[transport](args);

	LogError("Invalid transport name \"%s\"\n", transport.c_str());
	return NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command batching

/**
	@brief Pushes a command into the transmit FIFO then returns immediately.

	This command will actually be sent the next time FlushCommandQueue() is called.
 */
void SCPITransport::SendCommandQueued(const string& cmd)
{
	lock_guard<mutex> lock(m_queueMutex);

	//Do deduplication if there are existing queued commands
	if(!m_dedupCommands.empty() && !m_txQueue.empty())
	{
		//Parse the INCOMING command into sections

		//Split off subject, if we have one
		//(ignore leading colon)
		string tmp = cmd;
		size_t icolon;
		if(tmp[0] == ':')
			icolon = tmp.find(':', 1);
		else
			icolon = tmp.find(':', 0);
		string incoming_subject;
		if(icolon != string::npos)
		{
			incoming_subject = tmp.substr(0, icolon);
			tmp = tmp.substr(icolon + 1);
		}

		//Split off command from arguments
		size_t ispace = tmp.find(' ');
		string incoming_cmd;
		if(ispace != string::npos)
			incoming_cmd = tmp.substr(0, ispace);

		//Only attempt to deduplicate previous instances if this command is on the list of commands where it's OK
		if(m_dedupCommands.find(incoming_cmd) != m_dedupCommands.end())
		{
			auto it = m_txQueue.begin();
			while(it != m_txQueue.end())
			{
				tmp = *it;

				//Split off subject, if we have one
				//(ignore leading colon)
				if(tmp[0] == ':')
					icolon = tmp.find(':', 1);
				else
					icolon = tmp.find(':', 0);
				string subject;
				if(icolon != string::npos)
				{
					subject = tmp.substr(0, icolon);
					tmp = tmp.substr(icolon + 1);
				}

				//Split off command from arguments
				ispace = tmp.find(' ');
				string ncmd;
				if(ispace != string::npos)
					ncmd = tmp.substr(0, ispace);

				//Deduplicate if the same command is operating on the same subject
				if( (incoming_cmd == ncmd) && (incoming_subject == subject) )
				{
					LogTrace("Deduplicating redundant %s command %s and pushing new command %s\n",
						ncmd.c_str(),
						(*it).c_str(),
						cmd.c_str());

					auto oldit = it;
					it++;

					m_txQueue.erase(oldit);
				}

				//Nope, skip it
				else
					it++;
			}
		}

	}

	m_txQueue.push_back(cmd);

	LogTrace("%zu commands now queued\n", m_txQueue.size());
}

/**
	@brief Block until it's time to send the next command when rate limiting.
 */
void SCPITransport::RateLimitingWait()
{
	this_thread::sleep_until(m_nextCommandReady);
	m_nextCommandReady = chrono::system_clock::now() + m_rateLimitingInterval;
}

/**
	@brief Pushes all pending commands from SendCommandQueued() calls and blocks until they are all sent.
 */
bool SCPITransport::FlushCommandQueue()
{
	//Grab the queue, then immediately release the mutex so we can do more queued sends
	list<string> tmp;
	{
		lock_guard<mutex> lock(m_queueMutex);
		tmp = std::move(m_txQueue);
		m_txQueue.clear();
	}

	if(tmp.size())
		LogTrace("%zu commands being flushed\n", tmp.size());

	lock_guard<recursive_mutex> lock(m_netMutex);
	for(auto str : tmp)
	{
		if(m_rateLimitingEnabled)
			RateLimitingWait();
		SendCommand(str);
	}
	return true;
}

/**
	@brief Sends a command (flushing any pending/queued commands first), then returns the response.

	This is an atomic operation requiring no mutexing at the caller side.
 */
string SCPITransport::SendCommandQueuedWithReply(string cmd, bool endOnSemicolon)
{
	FlushCommandQueue();
	return SendCommandImmediateWithReply(cmd, endOnSemicolon);
}

/**
	@brief Sends a command (jumping ahead of the queue), then returns the response.

	This is an atomic operation requiring no mutexing at the caller side.
 */
string SCPITransport::SendCommandImmediateWithReply(string cmd, bool endOnSemicolon)
{
	lock_guard<recursive_mutex> lock(m_netMutex);

	if(m_rateLimitingEnabled)
		RateLimitingWait();

	SendCommand(cmd);

	return ReadReply(endOnSemicolon);
}

/**
	@brief Sends a command (jumping ahead of the queue) which does not require a response.
 */
void SCPITransport::SendCommandImmediate(string cmd)
{
	lock_guard<recursive_mutex> lock(m_netMutex);

	if(m_rateLimitingEnabled)
		RateLimitingWait();

	SendCommand(cmd);
}

/**
	@brief Sends a command (jumping ahead of the queue) which reads a binary block response
 */
void* SCPITransport::SendCommandImmediateWithRawBlockReply(string cmd, size_t& len)
{
	lock_guard<recursive_mutex> lock(m_netMutex);

	if(m_rateLimitingEnabled)
		RateLimitingWait();
	SendCommand(cmd);

	//Read the length
	char tmplen[3] = {0};
	if(2 != ReadRawData(2, (unsigned char*)tmplen))			//expect #n
		return NULL;
	if(tmplen[0] == 0)	//Not sure how this happens, but sometimes occurs on Tek MSO6?
		return NULL;
	size_t ndigits = stoull(tmplen+1);

	//Read the digits
	char digits[10] = {0};
	if(ndigits != ReadRawData(ndigits, (unsigned char*)digits))
		return NULL;
	len = stoull(digits);

	//Read the actual data
	unsigned char* buf = new unsigned char[len];
	len = ReadRawData(len, buf);
	return buf;
}

void SCPITransport::FlushRXBuffer(void)
{
	LogError("SCPITransport::FlushRXBuffer is unimplemented\n");
}
