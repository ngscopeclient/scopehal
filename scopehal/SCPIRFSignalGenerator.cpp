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

SCPIRFSignalGenerator::VSGCreateMapType SCPIRFSignalGenerator::m_vsgcreateprocs;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SCPIRFSignalGenerator::SCPIRFSignalGenerator()
{
}

SCPIRFSignalGenerator::~SCPIRFSignalGenerator()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Enumeration

void SCPIRFSignalGenerator::DoAddDriverClass(string name, VSGCreateProcType proc)
{
	m_vsgcreateprocs[name] = proc;
}

void SCPIRFSignalGenerator::EnumDrivers(vector<string>& names)
{
	for(auto it=m_vsgcreateprocs.begin(); it != m_vsgcreateprocs.end(); ++it)
		names.push_back(it->first);
}

shared_ptr<SCPIRFSignalGenerator> SCPIRFSignalGenerator::CreateRFSignalGenerator(string driver, SCPITransport* transport)
{
	if(m_vsgcreateprocs.find(driver) != m_vsgcreateprocs.end())
		return m_vsgcreateprocs[driver](transport);

	LogError("Invalid driver name \"%s\"\n", driver.c_str());
	return nullptr;
}
