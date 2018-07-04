/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2017 Andrew D. Zonenberg                                                                          *
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
	@brief Implementation of Oscilloscope
 */

#include "scopehal.h"
#include "OscilloscopeChannel.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h>

//#include "RigolDS1000SeriesOscilloscope.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction
Oscilloscope::Oscilloscope()
{

}

Oscilloscope::~Oscilloscope()
{
	for(size_t i=0; i<m_channels.size(); i++)
		delete m_channels[i];
	m_channels.clear();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device enumeration

/**
	@brief Gets the number of devices present
 */
int Oscilloscope::GetDeviceCount()
{
	/*
	//Search /dev for "usbtmc*"
	DIR* dir = opendir("/dev");
	dirent* dent = NULL;
	int usbtmc_max = -1;
	while(NULL != (dent = readdir(dir)))
	{
		//Skip everything but USB TMC
		if(NULL == strstr(dent->d_name, "usbtmc"))
			continue;

		//Read the count (may not be sorted)
		int tmp = -1;
		sscanf(dent->d_name, "usbtmc%3d", &tmp);

		//Keep track of the max
		if(tmp > usbtmc_max)
			usbtmc_max = tmp;
	}

	//Add 1 since indexes are zero-based
	closedir(dir);
	return usbtmc_max + 1;
	*/
	return 0;
}

/**
	@brief Creates the Nth device on the system

	@param ndev		Device index (zero based)
 */
Oscilloscope* Oscilloscope::CreateDevice(int /*ndev*/)
{
	/*
	//Open the file
	char fname[32];
	snprintf(fname, sizeof(fname), "/dev/usbtmc%d", ndev);
	int hfile = open(fname, O_RDWR);
	if(hfile < 0)
	{
		throw JtagExceptionWrapper(
			"Failed to open device file",
			"",
			JtagException::EXCEPTION_TYPE_ADAPTER);
	}

	//Ask the device for its ID
	write(hfile, "*IDN?", 5);
	char idcode[1024];
	int len = read(hfile, idcode, 1023);
	if(len <= 0)
	{
		throw JtagExceptionWrapper(
			"Failed to read ID code",
			"",
			JtagException::EXCEPTION_TYPE_ADAPTER);
	}
	idcode[len] = 0;

	//Parse ID code
	char vendor[1024];
	char model[1024];
	char serl[1024];
	char firmware[1024];
	sscanf(idcode, "%1023[^,],%1023[^,],%1023[^,],%1023s", vendor, model, serl, firmware);
	close(hfile);

	//Check vendors
	if(!strcmp(vendor, "Rigol Technologies"))
	{
		//Check model
		if(NULL != strstr(model, "DS1"))
			return new RigolDS1000SeriesOscilloscope(fname, serl);
		else
		{
			throw JtagExceptionWrapper(
				"Unrecognized Rigol device found - not supported",
				"",
				JtagException::EXCEPTION_TYPE_ADAPTER);
		}
	}
	*/
	throw JtagExceptionWrapper(
		"Unrecognized USBTMC vendor - not supported",
		"");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device properties

size_t Oscilloscope::GetChannelCount()
{
	return m_channels.size();
}

OscilloscopeChannel* Oscilloscope::GetChannel(size_t i)
{
	if(i < m_channels.size())
		return m_channels[i];
	else
	{
		throw JtagExceptionWrapper(
			"Invalid channel number",
			"");
	}
}

OscilloscopeChannel* Oscilloscope::GetChannel(std::string name, bool bThrowOnFailure)
{
	for(size_t i=0; i<m_channels.size(); i++)
	{
		if(m_channels[i]->m_displayname == name)
			return m_channels[i];
	}

	//not found
	if(bThrowOnFailure)
	{
		throw JtagExceptionWrapper(
			"Invalid channel name",
			"");
	}

	return NULL;
}

void Oscilloscope::AddChannel(OscilloscopeChannel* chan)
{
	m_channels.push_back(chan);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering helpers

bool Oscilloscope::WaitForTrigger(int timeout, bool exception_on_timeout)
{
	bool trig = false;
	for(int i=0; i<timeout*100 && !trig; i++)
	{
		trig = (PollTrigger() == Oscilloscope::TRIGGER_MODE_TRIGGERED);
		usleep(10 * 1000);
	}

	if(!trig && exception_on_timeout)
	{
		throw JtagExceptionWrapper(
			"Expected scope to trigger but it didn't",
			"");
	}

	return trig;
}
