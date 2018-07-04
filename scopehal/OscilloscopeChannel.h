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
	@brief Declaration of OscilloscopeChannel
 */

#ifndef OscilloscopeChannel_h
#define OscilloscopeChannel_h

#include "CaptureChannel.h"
class ChannelRenderer;

/**
	@brief A single channel on the oscilloscope.

	Each time the scope is triggered a new CaptureChannel is created with the new capture's data.
 */
class OscilloscopeChannel
{
public:
	enum ChannelType
	{
		CHANNEL_TYPE_ANALOG,
		CHANNEL_TYPE_DIGITAL,

		//Complex datatype from a protocol decoder
		CHANNEL_TYPE_COMPLEX
	};

	OscilloscopeChannel(std::string hwname, OscilloscopeChannel::ChannelType type, std::string color, int width = 1);
	virtual ~OscilloscopeChannel();

	///Display color (any valid GDK format)
	std::string m_displaycolor;

	///Display name (user defined, defaults to m_hwname)
	std::string m_displayname;

	//Stuff here is set once at init and can't be changed
	ChannelType GetType();
	std::string GetHwname();

	///Get the channel's data
	CaptureChannelBase* GetData();

	///Set new data, overwriting the old data as appropriate
	void SetData(CaptureChannelBase* pNew);

	virtual ChannelRenderer* CreateRenderer();

	///If not displayed OR used for trigger, may be disabled in the instrument if supported
	bool m_visible;

	int GetWidth();

	//Display time scale (normally the same for all channels)
	float m_timescale;

protected:

	///Capture data
	CaptureChannelBase* m_data;

	///Channel type
	ChannelType m_type;

	///Hardware name as labeled on the scope
	std::string m_hwname;

	///Bus width (1 to N, only meaningful for digital channels)
	int m_width;

	///Set to true if we're the output of a protocol decoder
	bool m_procedural;
};

#endif
