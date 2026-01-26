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
	@author Andrew D. Zonenberg
	@brief Implementation of VideoTrigger
	@ingroup triggers
 */

#include "scopehal.h"
#include "RSRTB2kVideoTrigger.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initialize the trigger

	@param scope	The scope this trigger will be used with
 */
RSRTB2kVideoTrigger::RSRTB2kVideoTrigger(Oscilloscope* scope)
	: Trigger(scope)
	, m_edgetype(m_parameters["Polarity"])
	, m_standardtype(m_parameters["Standard"])
	, m_modetype(m_parameters["Mode"])
	, m_linenumber(m_parameters["Line"])
	, m_holdofftimestate(m_parameters["Hold Off"])
	, m_holdofftime(m_parameters["Hold Off Time"])
{
	CreateInput("din");

	//Trigger level
	m_level.MarkHidden();
	m_triggerLevel.MarkHidden();
	m_upperLevel.MarkHidden();

	//Polarity
	m_edgetype = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_edgetype.AddEnumValue("Positive", EDGE_RISING);
	m_edgetype.AddEnumValue("Negative", EDGE_FALLING);

	//Standard
	m_standardtype = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_standardtype.AddEnumValue("PAL", STANDARD_PAL);
	m_standardtype.AddEnumValue("NTSC", STANDARD_NTSC);
	m_standardtype.AddEnumValue("SECAM", STANDARD_SEC);
	m_standardtype.AddEnumValue("PAL-M", STANDARD_PALM);
	m_standardtype.AddEnumValue("SDTV 576i", STANDARD_I576);
	m_standardtype.AddEnumValue("HDTV 720p", STANDARD_P720);
	m_standardtype.AddEnumValue("HDTV 1080p", STANDARD_P1080);
	m_standardtype.AddEnumValue("HDTV 1080i", STANDARD_I1080);

	//Mode
	m_modetype = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_modetype.AddEnumValue("All Frames", MODE_ALL);
	m_modetype.AddEnumValue("Odd Frames", MODE_ODD);
	m_modetype.AddEnumValue("Even Frames", MODE_EVEN);
	m_modetype.AddEnumValue("All Lines", MODE_ALIN);
	m_modetype.AddEnumValue("Line Number", MODE_LINE);

	//Line
	m_linenumber = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));

	//Hold off time
	m_holdofftimestate = FilterParameter(FilterParameter::TYPE_BOOL, Unit(Unit::UNIT_COUNTS));
	m_holdofftime = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_FS));
}

RSRTB2kVideoTrigger::~RSRTB2kVideoTrigger()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

///@brief Returns the constant trigger name "Video"
string RSRTB2kVideoTrigger::GetTriggerName()
{
	return "Video";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Input validation

bool RSRTB2kVideoTrigger::ValidateChannel(size_t i, StreamDescriptor stream)
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

	//It has to be analog or external trigger, digital inputs make no sense
	if((stream.GetType() != Stream::STREAM_TYPE_ANALOG) &&
		(stream.GetType() != Stream::STREAM_TYPE_TRIGGER))
	{
		return false;
	}

	return true;
}
