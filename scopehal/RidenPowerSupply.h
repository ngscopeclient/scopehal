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
	@brief Declaration of RidenPowerSupply
	@ingroup psudrivers
 */

#ifndef RidenPowerSupply_h
#define RidenPowerSupply_h

/**
	@brief A Riden RD6006 power supply or other equivalent model
	@ingroup psudrivers
 */
class RidenPowerSupply
	: public virtual SCPIPowerSupply
	, public virtual ModbusInstrument
{
public:
	RidenPowerSupply(SCPITransport* transport);
	virtual ~RidenPowerSupply();

	//Device information
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) const override;

	//Device capabilities
	virtual bool SupportsIndividualOutputSwitching() override;
	virtual bool SupportsVoltageCurrentControl(int chan) override;

	//Read sensors
	virtual double GetPowerVoltageActual(int chan) override;	//actual voltage after current limiting
	virtual double GetPowerVoltageNominal(int chan) override;	//set point
	virtual double GetPowerCurrentActual(int chan) override;	//actual current drawn by the load
	virtual double GetPowerCurrentNominal(int chan) override;	//current limit
	virtual bool GetPowerChannelActive(int chan) override;

	//Configuration
	virtual void SetPowerVoltage(int chan, double volts) override;
	virtual void SetPowerCurrent(int chan, double amps) override;
	virtual void SetPowerChannelActive(int chan, bool on) override;
	virtual bool IsPowerConstantCurrent(int chan) override;

protected:
	enum Registers : uint8_t
	{
		REGISTER_MODEL    = 0x00,
		REGISTER_SERIAL   = 0x02,
		REGISTER_FIRMWARE = 0x03,

		REGISTER_TEMP_C = 0x05,
		REGISTER_TEMP_F = 0x07,

		REGISTER_V_SET = 0x08,
		REGISTER_I_SET = 0x09,
		REGISTER_V_OUT = 0x0A,
		REGISTER_I_OUT = 0x0B,

		REGISTER_WATT    = 0x0D,
		REGISTER_V_INPUT = 0x0E,
		REGISTER_LOCK    = 0x0F,
		REGISTER_ERROR   = 0x10,

		REGISTER_ON_OFF  = 0x12
	};

	double m_currentFactor; 
	double m_voltageFactor; 

public:
	static std::string GetDriverNameInternal();
	static std::vector<SCPIInstrumentModel> GetDriverSupportedModels()
	{
		return {
	#ifdef _WIN32
        {"Riden RD60xx", {{ SCPITransportType::TRANSPORT_UART, "COM<x>" }}},
        {"Riden DPS30xx", {{ SCPITransportType::TRANSPORT_UART, "COM<x>" }}},
        {"Riden DPS50xx", {{ SCPITransportType::TRANSPORT_UART, "COM<x>" }}},
        {"Riden DPS80xx", {{ SCPITransportType::TRANSPORT_UART, "COM<x>" }}}
	#else
        {"Riden RD60xx", {{ SCPITransportType::TRANSPORT_UART, "/dev/ttyUSB<x>" }}},
        {"Riden DPS30xx", {{ SCPITransportType::TRANSPORT_UART, "/dev/ttyUSB<x>" }}},
        {"Riden DPS50xx", {{ SCPITransportType::TRANSPORT_UART, "/dev/ttyUSB<x>" }}},
        {"Riden DPS80xx", {{ SCPITransportType::TRANSPORT_UART, "/dev/ttyUSB<x>" }}}
	#endif
        };
	}
	POWER_INITPROC(RidenPowerSupply);
};

#endif
