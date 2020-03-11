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
	@brief Declaration of SCPITransport
 */

#ifndef SCPITransport_h
#define SCPITransport_h

/**
	@brief Abstraction of a transport layer for moving SCPI data between endpoints
 */
class SCPITransport
{
public:
	SCPITransport();
	virtual ~SCPITransport();

	virtual std::string GetConnectionString() =0;
	virtual std::string GetName() =0;

	virtual bool SendCommand(std::string cmd) =0;
	virtual std::string ReadReply() =0;
	virtual void ReadRawData(size_t len, unsigned char* buf) =0;
	virtual void SendRawData(size_t len, const unsigned char* buf) =0;

public:
	typedef SCPITransport* (*CreateProcType)(std::string args);
	static void DoAddTransportClass(std::string name, CreateProcType proc);

	static void EnumTransports(std::vector<std::string>& names);
	static SCPITransport* CreateTransport(std::string transport, std::string args);

protected:
	//Class enumeration
	typedef std::map< std::string, CreateProcType > CreateMapType;
	static CreateMapType m_createprocs;
};

#define TRANSPORT_INITPROC(T) \
	static SCPITransport* CreateInstance(std::string args) \
	{ \
		return new T(args); \
	} \
	virtual std::string GetName() \
	{ return GetTransportName(); }

#define AddTransportClass(T) SCPITransport::DoAddTransportClass(T::GetTransportName(), T::CreateInstance)

#endif
