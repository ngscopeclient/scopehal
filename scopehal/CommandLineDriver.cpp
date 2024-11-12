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

#include "scopehal.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

CommandLineDriver::CommandLineDriver(SCPITransport* transport) : SCPIDevice(transport, false)
{
}

CommandLineDriver::~CommandLineDriver()
{
}

size_t CommandLineDriver::ConverseMultiple(const std::string commandString, std::vector<string> &readLines, bool hasEcho, std::function<void(float)> progress, size_t expecedLines)
{
	stringstream ss(ConverseString(commandString,progress,expecedLines));
	string curLine;
	bool firstLine = true;
	size_t size = 0;
	while(getline(ss, curLine))
	{
		// Remove remaining \r
		curLine = Trim(curLine);
		if(hasEcho && firstLine)
		{
			// First line is always an echo of the sent command
			if(curLine.compare(commandString) != 0)
				LogWarning(
					"Unexpected response \"%s\" to command string \"%s\".\n", curLine.c_str(), commandString.c_str());
			firstLine = false;
		}
		else if(!curLine.empty())
		{
			LogTrace("Pusshing back line \"%s\".\n", curLine.c_str());
			readLines.push_back(curLine);
			size++;
		}
	}
	return size;
}

std::string CommandLineDriver::ConverseSingle(const std::string commandString, bool hasEcho)
{
	stringstream ss(ConverseString(commandString));
	string result;
	if(hasEcho)
	{
		// Read first line (echo of command string)
		getline(ss, result);
		// Remove remaining \r
		result = Trim(result);
		if(result.compare(commandString) != 0)
			LogWarning("Unexpected response \"%s\" to command string \"%s\".\n", result.c_str(), commandString.c_str());
	}
	// Get second line as result
	getline(ss, result);
	// Remove remaining \r
	result = Trim(result);
	return result;
}

std::string CommandLineDriver::ConverseString(const std::string commandString, std::function<void(float)> progress, size_t expecedLines)
{
	string result = "";
	// Lock guard
	LogTrace("Sending command: '%s'.\n",commandString.c_str());
	lock_guard<recursive_mutex> lock(m_transportMutex);
	m_transport->SendCommand(commandString+"\r\n");
	// Read until we get  "ch>\r\n"
	char tmp = ' ';
	size_t bytesRead = 0;
	size_t linesRead = 0;
	double start = GetTime();
	while(true)
	{	
		// Consume response until we find the end delimiter
		if(!m_transport->ReadRawData(1,(unsigned char*)&tmp))
		{
			// We might have to wait for a bit to get a response
			if(GetTime()-start >= m_communicationTimeout)
			{
				// Timeout
				LogError("A timeout occurred while reading data from device.\n");
				break;
			}
			continue;
		}
		result += tmp;
		if(progress && (tmp == '\n'))
		{
			linesRead++;
			progress((float)linesRead/(float)expecedLines);
		}
		bytesRead++;
		if(bytesRead > m_maxResponseSize)
		{
			LogError("Error while reading data from device: response too long (%zu bytes).\n",bytesRead);
			break;
		}
		if(result.size()>=TRAILER_STRING_LENGTH && (0 == result.compare (result.length() - TRAILER_STRING_LENGTH, TRAILER_STRING_LENGTH, TRAILER_STRING)))
			break;
	}
	//LogDebug("Received: %s\n",result.c_str());
	return result;
}

bool CommandLineDriver::ConverseSweep(int64_t &sweepStart, int64_t &sweepStop,[[maybe_unused]] int64_t &points, bool setValue)
{
	size_t lines;
	vector<string> reply;
	int64_t origStartValue = sweepStart;
	int64_t origStopValue = sweepStop;
	if(setValue)
	{
		// Send start value
		lines = ConverseMultiple("sweep start "+std::to_string(sweepStart),reply);
		if(lines > 1)
		{	// Value was rejected
			LogWarning("Error while sending sweep start value %" PRIi64 ": \"%s\".\n",sweepStart,reply[0].c_str());
		}
		// Send stop value
		lines = ConverseMultiple("sweep stop "+ std::to_string(sweepStop) ,reply);
		if(lines > 1)
		{	// Value was rejected
			LogWarning("Error while sending sweep stop value %" PRIi64 ": \"%s\".\n",sweepStop,reply[0].c_str());
		}
		// Clear reply for next use
		reply.clear();
	}
	// Get currently configured sweep
	lines = ConverseMultiple("sweep",reply);
	if(lines < 1)
	{
		LogWarning("Error while requesting sweep values: no lines returned.\n");
		return false;
	}
	sscanf(reply[0].c_str(), "%" SCNi64 " %" SCNi64 " %" SCNi64, &sweepStart, &sweepStop, &points);
	LogDebug("Found sweep start %" PRIi64 " / stop %" PRIi64 ".\n",sweepStart,sweepStop);
	return setValue && ((origStartValue != sweepStart) || (origStopValue != sweepStop));
}

std::string CommandLineDriver::DrainTransport()
{
	string result;
	char tmp = ' ';
	size_t bytesRead = 0;
	while(true)
	{	
		// Consume response until we find the end delimiter
		if(!m_transport->ReadRawData(1,(unsigned char*)&tmp))
		{
			break;
		}
		result += tmp;
		bytesRead++;
		if(bytesRead > m_maxResponseSize)
		{
			LogError("Error while reading data from device: response too long (%zu bytes).\n",bytesRead);
			break;
		}
	}
	LogTrace("Drained data from console transport: %s\n",result.c_str());
	return result;
}
