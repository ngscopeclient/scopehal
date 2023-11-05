/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2023 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Implementation of PausableFilter
 */

#include "scopehal.h"
#include "ActionProvider.h"
#include "PausableFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PausableFilter::PausableFilter(const string& color, Category cat, Unit xunit)
	: Filter(color, cat, xunit)
	, m_running(true)
	, m_oneShot(false)
{
}

PausableFilter::~PausableFilter()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pausing / unpausing

vector<string> PausableFilter::EnumActions()
{
	vector<string> ret;
	ret.push_back("Run");
	ret.push_back("Single");
	ret.push_back("Stop");
	return ret;
}

void PausableFilter::Run()
{
	m_running = true;
	m_oneShot = false;
}

void PausableFilter::Single()
{
	m_running = true;
	m_oneShot = true;
}

void PausableFilter::Stop()
{
	m_running = false;
	m_oneShot = false;
}

bool PausableFilter::PerformAction(const string& id)
{
	if(id == "Run")
		Run();
	else if(id == "Single")
		Single();
	else if(id == "Stop")
		Stop();
	else
		LogError("PausableFilter: unrecognized action\n");

	return false;
}

bool PausableFilter::ShouldRefresh()
{
	//Running?
	if(m_running)
	{
		//Single shot trigger?
		if(m_oneShot)
			m_running = false;

		//Either way, refresh this time
		return true;
	}

	//otherwise don't do anything
	return false;
}
