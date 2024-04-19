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

#include "scopehal.h"

using namespace std;

SCPIBERT::BERTCreateMapType SCPIBERT::m_bertcreateprocs;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SCPIBERT::SCPIBERT()
{
}

SCPIBERT::~SCPIBERT()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Enumeration

void SCPIBERT::DoAddDriverClass(string name, BERTCreateProcType proc)
{
	m_bertcreateprocs[name] = proc;
}

void SCPIBERT::EnumDrivers(vector<string>& names)
{
	for(auto it=m_bertcreateprocs.begin(); it != m_bertcreateprocs.end(); ++it)
		names.push_back(it->first);
}

shared_ptr<SCPIBERT> SCPIBERT::CreateBERT(string driver, SCPITransport* transport)
{
	if(m_bertcreateprocs.find(driver) != m_bertcreateprocs.end())
	{
		auto ret = m_bertcreateprocs[driver](transport);
		ret->PostCtorInit();
		return ret;
	}

	LogError("Invalid driver name \"%s\"\n", driver.c_str());
	return nullptr;
}
