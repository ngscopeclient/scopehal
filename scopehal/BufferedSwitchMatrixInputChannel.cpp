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
	@brief Implementation of BufferedSwitchMatrixInputChannel
	@ingroup core
 */
#include "scopehal.h"
#include "BufferedSwitchMatrixInputChannel.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initialize the channel

	@param hwname	Hardware name of the channel
	@param parent	Switch matrix the channel is part of
	@param color	Initial display color of the channel
	@param index	Number of the channel
 */
BufferedSwitchMatrixInputChannel::BufferedSwitchMatrixInputChannel(
	const string& hwname,
	SwitchMatrix* parent,
	const string& color,
	size_t index)
	: DigitalInputChannel(hwname, parent, color, index)
{
}

BufferedSwitchMatrixInputChannel::~BufferedSwitchMatrixInputChannel()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Vertical scaling and stream management

bool BufferedSwitchMatrixInputChannel::ValidateChannel(
	[[maybe_unused]] size_t i,
	[[maybe_unused]] StreamDescriptor stream)
{
	//no inputs allowed to an input
	return false;
}

void BufferedSwitchMatrixInputChannel::OnInputChanged([[maybe_unused]] size_t i)
{

}
