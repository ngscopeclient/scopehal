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
	@brief Declaration of Instrument
 */

#ifndef Instrument_h
#define Instrument_h

/**
	@brief An arbitrary lab instrument. Oscilloscope, LA, PSU, DMM, etc
 */
class Instrument
{
public:
	virtual ~Instrument();

	/*
		@brief Types of instrument.

		Note that we can't use RTTI for this because of software options that may or may not be present,
		and we don't know at object instantiation time.

		For example, some WaveSurfer 3000 devices have the function generator option and others don't.
		While the WaveSurfer 3000 DMM option is now no-cost, there's no guarantee any given instrument's
		owner has installed it!
	 */
	enum InstrumentTypes
	{
		//An oscilloscope or logic analyzer
		INST_OSCILLOSCOPE 		= 1,

		//A multimeter (query to see what measurements it supports)
		INST_DMM 				= 2,

		//A power supply
		INST_PSU				= 4,
	};

	virtual unsigned int GetInstrumentTypes() =0;

	//Device information
	virtual std::string GetName() =0;
	virtual std::string GetVendor() =0;
	virtual std::string GetSerial() =0;
};

#endif
