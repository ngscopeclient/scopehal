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

#ifndef RohdeSchwarzHMC804xPowerSupply_h
#define RohdeSchwarzHMC804xPowerSupply_h


#include "../xptools/Socket.h"

/**
	@brief A Rohde & Schwarz HMC804x power supply
 */
class RohdeSchwarzHMC804xPowerSupply
	: public virtual PowerSupply
{
public:
	RohdeSchwarzHMC804xPowerSupply(std::string hostname, unsigned short port);
	virtual ~RohdeSchwarzHMC804xPowerSupply();

	//Device information
	virtual std::string GetName();
	virtual std::string GetVendor();
	virtual std::string GetSerial();

	virtual unsigned int GetInstrumentTypes();

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

protected:
	int GetStatusRegister(int chan);

	Socket m_socket;

	std::string m_hostname;
	unsigned short m_port;

	std::string m_vendor;
	std::string m_model;
	std::string m_serial;
	std::string m_hwVersion;
	std::string m_fwVersion;

	//Helpers for controlling stuff
	bool SelectChannel(int chan);

	int m_channelCount;

	bool SendCommand(std::string cmd);
	std::string ReadReply();

	int m_activeChannel;
};

#endif
