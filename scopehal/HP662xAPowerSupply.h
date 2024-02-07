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

#ifndef HP662xAPowerSupply_h
#define HP662xAPowerSupply_h

/**
	@brief A HP 662xA power supply
 */
class HP662xAPowerSupply
	: public virtual SCPIPowerSupply
	, public virtual SCPIDevice
{
public:
	HP662xAPowerSupply(SCPITransport* transport);
	virtual ~HP662xAPowerSupply();

	//Device information
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) const override;

	//Device capabilities
	bool SupportsSoftStart() override;
	bool SupportsIndividualOutputSwitching() override;
	bool SupportsMasterOutputSwitching() override;
	bool SupportsOvercurrentShutdown() override;

	//Read sensors
	double GetPowerVoltageActual(int chan) override;	//actual voltage after current limiting
	double GetPowerVoltageNominal(int chan) override;	//set point
	double GetPowerCurrentActual(int chan) override;	//actual current drawn by the load
	double GetPowerCurrentNominal(int chan) override;	//current limit
	bool GetPowerChannelActive(int chan) override;

	//Configuration
	bool GetPowerOvercurrentShutdownEnabled(int chan) override;	//shut channel off entirely on overload,
																//rather than current limiting
	void SetPowerOvercurrentShutdownEnabled(int chan, bool enable) override;
	bool GetPowerOvercurrentShutdownTripped(int chan) override;
	void SetPowerVoltage(int chan, double volts) override;
	void SetPowerCurrent(int chan, double amps) override;
	void SetPowerChannelActive(int chan, bool on) override;
	bool IsPowerConstantCurrent(int chan) override;

protected:
	std::string ChannelQuery(const char* query, int chan);
	void ChannelCommand(const char* command, int chan);
	void ChannelCommand(const char* command, int chan, int arg);
	void ChannelCommand(const char* command, int chan, double arg);
	int GetStatusRegister(int chan);


	int m_activeChannel;

public:
	static std::string GetDriverNameInternal();
	POWER_INITPROC(HP662xAPowerSupply)
};

#endif
