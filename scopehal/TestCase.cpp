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
	@brief Implementation of TestWaveformSource
 */

#include "scopehal.h"
#include "TestCase.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

TestCase::TestCase(int argc, char* argv[], string filter)
	: m_scope("Test Scope", "Antikernel Labs", "12345")
{
	//Global settings
	//Default to debug since this is a test case
	Severity console_verbosity = Severity::DEBUG;

	//Parse command-line arguments
	int64_t seed = 0;
	for(int i=1; i<argc; i++)
	{
		string s(argv[i]);

		//Let the logger eat its args first
		if(ParseLoggerArguments(i, argc, argv, console_verbosity))
			continue;

		//TODO: parse arguments for random seed, number of iterations, etc
	}

	//Initialize the seed
	m_rng.seed(seed);

	//Set up logging
	g_log_sinks.emplace(g_log_sinks.begin(), new ColoredSTDLogSink(console_verbosity));

	//Global scopehal initialization
	TransportStaticInit();
	DriverStaticInit();
	InitializePlugins();

	//Create the filter
	m_filter = Filter::CreateFilter(filter, "#ffffff");
	m_filter->AddRef();

	LogNotice("Testing \"%s\" filter\n", filter.c_str());
}

TestCase::~TestCase()
{
	m_filter->Release();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test execution

/**
	@brief Runs the test iteration in a loop

	@return True if all test iterations are successful, false on failure
 */
bool TestCase::Run()
{
	//TODO: get this from an argument
	const size_t niter = 25;

	for(size_t i=0; i<niter; i++)
	{
		if(!Iteration(i))
		{
			LogError("Test case FAILED at iteration %zu\n", i);
			return false;
		}
	}

	return true;
}
