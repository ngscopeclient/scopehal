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

#ifndef GWInstekGPDX303SPowerSupply_h
#define GWInstekGPDX303SPowerSupply_h

#include "SCPIDevice.h"
#include "SCPIPowerSupply.h"
#include "SCPITransport.h"

#include <bitset>
#include <string>

/**
	@brief A GW Instek GPD-(X)303S power supply
 */
class GWInstekGPDX303SPowerSupply
	: public virtual SCPIPowerSupply
	, public virtual SCPIDevice
{
public:
	GWInstekGPDX303SPowerSupply(SCPITransport* transport);
	virtual ~GWInstekGPDX303SPowerSupply();

	//Device information
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) const override;

	//Device capabilities
	bool SupportsMasterOutputSwitching() override;

	//Read sensors
	double GetPowerVoltageActual(int chan) override;	//actual voltage after current limiting
	double GetPowerVoltageNominal(int chan) override;	//set point
	double GetPowerCurrentActual(int chan) override;	//actual current drawn by the load
	double GetPowerCurrentNominal(int chan) override;	//current limit

	//Configuration
	void SetPowerVoltage(int chan, double volts) override;
	void SetPowerCurrent(int chan, double amps) override;
	bool IsPowerConstantCurrent(int chan) override;

	bool GetMasterPowerEnable() override;
	void SetMasterPowerEnable(bool enable) override;

protected:
	std::bitset<8> GetStatusRegister();

public:
	static std::string GetDriverNameInternal();
	POWER_INITPROC(GWInstekGPDX303SPowerSupply)
};

#endif
