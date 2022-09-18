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

#ifndef GWInstekGPDX303SPowerSupply_h
#define GWInstekGPDX303SPowerSupply_h

#include "SCPIDevice.h"
#include "SCPIPowerSupply.h"
#include "SCPITransport.h"

#include <string>

/**
	@brief A GW Instek GPD-(X)303S power supply
 */
class GWInstekGPDX303SPowerSupply
	: public virtual SCPIPowerSupply
	, public virtual SCPIDevice
{
    GWInstekGPDX303SPowerSupply(SCPITransport* transport);
	virtual ~GWInstekGPDX303SPowerSupply();

	//Device information
	virtual std::string GetName();
	virtual std::string GetVendor();
	virtual std::string GetSerial();

	virtual unsigned int GetInstrumentTypes();

    //Device capabilities
	virtual bool SupportsSoftStart();
	virtual bool SupportsIndividualOutputSwitching();
	virtual bool SupportsMasterOutputSwitching();
	virtual bool SupportsOvercurrentShutdown();

	//Channel info
	virtual int GetPowerChannelCount();
	virtual std::string GetPowerChannelName(int chan);

	//Read sensors
	virtual double GetPowerVoltageActual(int chan);				//actual voltage after current limiting
	virtual double GetPowerVoltageNominal(int chan);			//set point
	virtual double GetPowerCurrentActual(int chan);				//actual current drawn by the load
	virtual double GetPowerCurrentNominal(int chan);			//current limit
	virtual bool GetPowerChannelActive(int chan);

	//Configuration
	virtual bool GetPowerOvercurrentShutdownEnabled(int chan);	//shut channel off entirely on overload,
																//rather than current limiting
	virtual void SetPowerOvercurrentShutdownEnabled(int chan, bool enable);
	virtual bool GetPowerOvercurrentShutdownTripped(int chan);
	virtual void SetPowerVoltage(int chan, double volts);
	virtual void SetPowerCurrent(int chan, double amps);
	virtual void SetPowerChannelActive(int chan, bool on);
	virtual bool IsPowerConstantCurrent(int chan);

	virtual bool GetMasterPowerEnable();
	virtual void SetMasterPowerEnable(bool enable);

	virtual bool IsSoftStartEnabled(int chan);
	virtual void SetSoftStartEnabled(int chan, bool enable);

protected:
	uint8_t GetStatusRegister();

	int m_channelCount;

public:
	static std::string GetDriverNameInternal();
	POWER_INITPROC(GWInstekGPDX303SPowerSupply)
};

#endif
