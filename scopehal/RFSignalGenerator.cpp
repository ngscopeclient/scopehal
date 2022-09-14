/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2021 Andrew D. Zonenberg and contributors                                                         *
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

RFSignalGenerator::RFSignalGenerator()
{
}

RFSignalGenerator::~RFSignalGenerator()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Placeholder implementations for optional features not all instruments may have

float RFSignalGenerator::GetSweepStartFrequency(int /*chan*/)
{
	return 0;
}

float RFSignalGenerator::GetSweepStopFrequency(int /*chan*/)
{
	return 0;
}

void RFSignalGenerator::SetSweepStartFrequency(int /*chan*/, float /*freq*/)
{
	//no-op in base class
}

void RFSignalGenerator::SetSweepStopFrequency(int /*chan*/, float /*freq*/)
{
	//no-op in base class
}

float RFSignalGenerator::GetSweepStartLevel(int /*chan*/)
{
	return 0;
}

float RFSignalGenerator::GetSweepStopLevel(int /*chan*/)
{
	return 0;
}

void RFSignalGenerator::SetSweepStartLevel(int /*chan*/, float /*level*/)
{
	//no-op in base class
}

void RFSignalGenerator::SetSweepStopLevel(int /*chan*/, float /*level*/)
{
	//no-op in base class
}

float RFSignalGenerator::GetSweepDwellTime(int /*chan*/)
{
	return 0;
}

void RFSignalGenerator::SetSweepDwellTime(int /*chan*/, float /*fs*/)
{
	//no-op in base class
}

void RFSignalGenerator::SetSweepPoints(int /*chan*/, int /*npoints*/)
{
}

int RFSignalGenerator::GetSweepPoints(int /*chan*/)
{
	return 0;
}

RFSignalGenerator::SweepShape RFSignalGenerator::GetSweepShape(int /*chan*/)
{
	return SWEEP_SHAPE_TRIANGLE;
}

void RFSignalGenerator::SetSweepShape(int /*chan*/, SweepShape /*shape*/)
{
	//no-op in base class
}

RFSignalGenerator::SweepSpacing RFSignalGenerator::GetSweepSpacing(int /*chan*/)
{
	return SWEEP_SPACING_LINEAR;
}

void RFSignalGenerator::SetSweepSpacing(int /*chan*/, SweepSpacing /*shape*/)
{
	//no-op in base class
}

RFSignalGenerator::SweepDirection RFSignalGenerator::GetSweepDirection(int /*chan*/)
{
	return SWEEP_DIR_FWD;
}

void RFSignalGenerator::SetSweepDirection(int /*chan*/, SweepDirection /*dir*/)
{
	//no-op in base class
}

RFSignalGenerator::SweepType RFSignalGenerator::GetSweepType(int /*chan*/)
{
	return SWEEP_TYPE_NONE;
}

void RFSignalGenerator::SetSweepType(int /*chan*/, SweepType /*type*/)
{
	//no-op in base class
}
