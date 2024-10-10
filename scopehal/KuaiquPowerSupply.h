/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2023 Andrew D. Zonenberg and contributors                                                         *
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

#ifndef KuaiquPowerSupply_h
#define KuaiquPowerSupply_h

/**
	@brief A KUAIQU power supply
 */
class KuaiquPowerSupply
	: public virtual SCPIPowerSupply
	, public virtual SCPIInstrument
{
public:
	KuaiquPowerSupply(SCPITransport* transport);
	virtual ~KuaiquPowerSupply();

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
	enum Command : char {
		COMMAND_WRITE_VOLTAGE= '1',
		COMMAND_READ_VOLTAGE = '2',
		COMMAND_WRITE_CURRENT= '3',
		COMMAND_READ_CURRENT = '4',
		COMMAND_KEYPAD_ECHO  = '5',
		COMMAND_FIRMWARE	 = '6',
		COMMAND_ON			 = '7', // No reply to off command
		COMMAND_OFF			 = '8', // No reply to on command
		COMMAND_LOCK		 = '9',
		COMMAND_LOCK_ON,
		COMMAND_LOCK_OFF
	};
	// Make sure several request don't collide before we received the corresponding response
	std::recursive_mutex m_transportMutex;
	// Rate limiting as per documentation : 3.5 bytes at 9600 bauds = 3.5 * 1.04ms = 3.64ms => 4 ms
	std::chrono::milliseconds m_rateLimitingInterval = std::chrono::milliseconds(4);
	std::chrono::system_clock::time_point m_nextCommandReady = std::chrono::system_clock::now();


	bool SendWriteValueCommand(Command command, double value);
	double SendReadValueCommand(Command command);
	std::string SendSimpleCommand(Command command);
	std::string SendCommand(Command command, std::string commandString);

	// PSU state
	bool m_on = false;
	double m_current = 0;
	double m_voltage = 0;
	bool m_constantCurrent = false;

public:
	static std::string GetDriverNameInternal();
	POWER_INITPROC(KuaiquPowerSupply);
};

#endif
