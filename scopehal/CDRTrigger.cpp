/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2022 Andrew D. Zonenberg and contributors                                                         *
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
#include "CDRTrigger.h"
#include "LeCroyOscilloscope.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

CDRTrigger::CDRTrigger(Oscilloscope* scope)
	: Trigger(scope)
	, m_bitRateName("Bit Rate")
{
	CreateInput("in");

	m_parameters[m_bitRateName] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_BITRATE));
	m_parameters[m_bitRateName].SetIntVal(1250000000);
}

CDRTrigger::~CDRTrigger()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Autobaud

bool CDRTrigger::IsAutomaticBitRateCalculationAvailable()
{
	if(dynamic_cast<LeCroyOscilloscope*>(m_scope) != nullptr)
		return true;

	return false;
}

bool CDRTrigger::ValidateChannel(size_t /*i*/, StreamDescriptor stream)
{
	//LeCroy scopes with CDR trigger only support it on channel 4 (if not interleaving) or 3 (if interleaving)
	if(dynamic_cast<LeCroyOscilloscope*>(m_scope) != nullptr)
	{
		int expectedChannel = 3;
		if(m_scope->IsInterleaving())
			expectedChannel = 2;

		return (stream.m_channel == m_scope->GetChannel(expectedChannel));
	}

	return true;
}
