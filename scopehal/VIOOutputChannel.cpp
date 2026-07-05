/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Implementation of VIOOutputChannel
	@ingroup core
 */

#include "scopehal.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initialize a digital GPIO channel

	@param hwname	Internal hardware name of the channel (should match SCPI name if applicable)
	@param parent	The instrument this channel is part of
	@param color	Display color for the channel in plots and the filter graph
	@param index	Position of this channel within m_channels of the parent instrument
 */
VIOOutputChannel::VIOOutputChannel(
	const string& hwname,
	Instrument* parent,
	const string& color,
	size_t index,
	size_t width)
	: InstrumentChannel(parent, hwname, color, Unit(Unit::UNIT_FS), index)
{
	ClearStreams();

	//Set up channels
	CreateInput<InputConstraintAND>(
		"dout",
		initializer_list<shared_ptr<InputConstraint> >
		{
			make_shared<InputConstraintDigitalWidth>(this, width),
			make_shared<InputConstraintStreamType>(this, Stream::STREAM_TYPE_DIGITAL_SCALAR)
		});
}

VIOOutputChannel::~VIOOutputChannel()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Vertical scaling and stream management

InstrumentChannel::PhysicalConnector VIOOutputChannel::GetPhysicalConnector()
{
	return CONNECTOR_SMA;
}
