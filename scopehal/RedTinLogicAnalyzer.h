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
	@brief Declaration of RedTinLogicAnalyzer
 */

#ifndef RedTinLogicAnalyzer_h
#define RedTinLogicAnalyzer_h

#include "../xptools/UART.h"

class RedTinLogicAnalyzer : public Oscilloscope
{
public:
	RedTinLogicAnalyzer(const std::string& tty, int baud);

	/*
	RedTinLogicAnalyzer(const std::string& host, unsigned short port, const std::string& nochost);
	RedTinLogicAnalyzer(const std::string& host, unsigned short port);
	void Connect(const std::string& nochost);
	*/
	virtual ~RedTinLogicAnalyzer();

	virtual std::string GetName();
	virtual std::string GetVendor();
	virtual std::string GetSerial();

	//Triggering
	virtual Oscilloscope::TriggerMode PollTrigger();
	virtual bool AcquireData(sigc::slot1<int, float> progress_callback);
	virtual void Start();
	virtual void StartSingleTrigger();
	virtual void Stop();

	virtual void ResetTriggerConditions();
	virtual void SetTriggerForChannel(OscilloscopeChannel* channel, std::vector<TriggerType> triggerbits);

	virtual unsigned int GetInstrumentTypes();

protected:
	enum Transport
	{
		TRANSPORT_UART,
		TRANSPORT_NOC
	} m_transport;

	void LoadChannels();
	bool Ping();

	//std::string ReadString(const unsigned char* data, int& pos);

	std::string m_laname;
	//uint16_t m_scopeaddr;

	std::vector<int> m_triggers;

	uint32_t m_timescale;
	uint32_t m_depth;
	uint32_t m_width;

	UART* m_uart;
	//NOCSwitchInterface m_iface;
	//NameServer* m_nameserver;
};

#endif

