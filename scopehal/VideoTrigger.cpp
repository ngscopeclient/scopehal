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

#include "scopehal.h"
#include "VideoTrigger.h"
#include "LeCroyOscilloscope.h"
#include "TektronixOscilloscope.h"
#include "SiglentSCPIOscilloscope.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

VideoTrigger::VideoTrigger(Oscilloscope* scope) : Trigger(scope)
{
	CreateInput("din");

	m_standardname = "Standard";
	m_parameters[m_standardname] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_standardname].AddEnumValue("NTSC", NTSC);
	m_parameters[m_standardname].AddEnumValue("PAL", PAL);
	m_parameters[m_standardname].AddEnumValue("720p50", P720L50);
	m_parameters[m_standardname].AddEnumValue("720p60", P720L60);
	m_parameters[m_standardname].AddEnumValue("1080p50", P1080L50);
	m_parameters[m_standardname].AddEnumValue("1080p60", P1080L60);
	m_parameters[m_standardname].AddEnumValue("1080i50", I1080L50);
	m_parameters[m_standardname].AddEnumValue("1080i60", I1080L60);
	m_parameters[m_standardname].AddEnumValue("Custom", CUSTOM);

	m_syncmode = "Sync Mode";
	m_parameters[m_syncmode] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_syncmode].AddEnumValue("Any", ANY);
	m_parameters[m_syncmode].AddEnumValue("Select", SELECT);

	m_linename = "Line";
	m_parameters[m_linename] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));

	m_fieldname = "Field";
	m_parameters[m_fieldname] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));

	m_framerate = "Custom Frame Rate";
	m_parameters[m_framerate] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_framerate].AddEnumValue("25Hz", FRAMERATE_25HZ);
	m_parameters[m_framerate].AddEnumValue("30Hz", FRAMERATE_30HZ);
	m_parameters[m_framerate].AddEnumValue("50Hz", FRAMERATE_50HZ);
	m_parameters[m_framerate].AddEnumValue("60Hz", FRAMERATE_60HZ);

	m_interlace = "Custom Interlace";
	m_parameters[m_interlace] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));

	m_linecount = "Custom Number of Lines";
	m_parameters[m_linecount] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));

	m_fieldcount = "Custom Number of Fields";
	m_parameters[m_fieldcount] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
}

VideoTrigger::~VideoTrigger()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string VideoTrigger::GetTriggerName()
{
	return "Video";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Input validation

bool VideoTrigger::ValidateChannel(size_t i, StreamDescriptor stream)
{
	//We only can take one input
	if(i > 0)
		return false;

	//There has to be a signal to trigger on
	auto schan = dynamic_cast<OscilloscopeChannel*>(stream.m_channel);
	if(!schan)
		return false;

	//It has to be from the same instrument we're trying to trigger on
	if(schan->GetScope() != m_scope)
		return false;

	return true;
}
