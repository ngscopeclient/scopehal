/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2019 Andrew D. Zonenberg                                                                          *
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

#ifndef LeCroyOscilloscope_h
#define LeCroyOscilloscope_h

#include "../xptools/Socket.h"

/**
	@brief A LeCroy VICP oscilloscope

	Protocol layer is based on LeCroy's released VICPClient.h, but rewritten and modernized heavily
 */
class LeCroyOscilloscope
	: public virtual Oscilloscope
	, public virtual Multimeter
{
public:
	LeCroyOscilloscope(std::string hostname, unsigned short port);
	virtual ~LeCroyOscilloscope();

protected:
	void IdentifyHardware();
	void SharedCtorInit();
	virtual void DetectAnalogChannels();

public:

	//Device information
	virtual std::string GetName();
	virtual std::string GetVendor();
	virtual std::string GetSerial();
	virtual unsigned int GetInstrumentTypes();
	virtual unsigned int GetMeasurementTypes();

	virtual void FlushConfigCache();

	//Channel configuration
	virtual bool IsChannelEnabled(size_t i);
	virtual void EnableChannel(size_t i);
	virtual void DisableChannel(size_t i);
	virtual OscilloscopeChannel::CouplingType GetChannelCoupling(size_t i);
	virtual void SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type);
	virtual double GetChannelAttenuation(size_t i);
	virtual void SetChannelAttenuation(size_t i, double atten);
	virtual int GetChannelBandwidthLimit(size_t i);
	virtual void SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz);
	virtual double GetChannelVoltageRange(size_t i);
	virtual void SetChannelVoltageRange(size_t i, double range);
	virtual OscilloscopeChannel* GetExternalTrigger();
	virtual double GetChannelOffset(size_t i);
	virtual void SetChannelOffset(size_t i, double offset);

	//Triggering
	virtual void ResetTriggerConditions();
	virtual Oscilloscope::TriggerMode PollTrigger();
	virtual bool AcquireData(sigc::slot1<int, float> progress_callback);
	virtual void Start();
	virtual void StartSingleTrigger();
	virtual void Stop();
	virtual size_t GetTriggerChannelIndex();
	virtual void SetTriggerChannelIndex(size_t i);
	virtual float GetTriggerVoltage();
	virtual void SetTriggerVoltage(float v);
	virtual Oscilloscope::TriggerType GetTriggerType();
	virtual void SetTriggerType(Oscilloscope::TriggerType type);
	virtual void SetTriggerForChannel(OscilloscopeChannel* channel, std::vector<TriggerType> triggerbits);

	//VICP constant helpers
	enum HEADER_OPS
	{
		OP_DATA		= 0x80,
		OP_REMOTE	= 0x40,
		OP_LOCKOUT	= 0x20,
		OP_CLEAR	= 0x10,
		OP_SRQ		= 0x8,
		OP_REQ		= 0x4,
		OP_EOI		= 0x1
	};

	//DMM acquisition
	virtual double GetVoltage();
	virtual double GetPeakToPeak();
	virtual double GetFrequency();
	virtual double GetCurrent();

	//DMM configuration
	virtual int GetMeterChannelCount();
	virtual std::string GetMeterChannelName(int chan);
	virtual int GetCurrentMeterChannel();
	virtual void SetCurrentMeterChannel(int chan);
	virtual void StartMeter();
	virtual void StopMeter();
	virtual void SetMeterAutoRange(bool enable);
	virtual bool GetMeterAutoRange();

	virtual Multimeter::MeasurementTypes GetMeterMode();
	virtual void SetMeterMode(Multimeter::MeasurementTypes type);

protected:
	Socket m_socket;

	void BulkCheckChannelEnableState();

	virtual bool SendCommand(std::string cmd, bool eoi=true) = 0;
	virtual std::string ReadData() =0;
	virtual std::string ReadMultiBlockString() =0;
	virtual std::string ReadSingleBlockString(bool trimNewline = false) =0;

	bool ReadWaveformBlock(std::string& data);

	std::string m_hostname;
	unsigned short m_port;

	//hardware analog channel count, independent of LA option or protocol decodes
	unsigned int m_analogChannelCount;
	unsigned int m_digitalChannelCount;

	std::string m_vendor;
	std::string m_model;
	std::string m_serial;
	std::string m_fwVersion;

	//set of SW/HW options we have
	bool m_hasLA;
	bool m_hasDVM;

	//Cached configuration
	bool m_triggerChannelValid;
	size_t m_triggerChannel;
	bool m_triggerLevelValid;
	float m_triggerLevel;
	bool m_triggerTypeValid;
	TriggerType m_triggerType;
	std::map<size_t, double> m_channelVoltageRanges;
	std::map<size_t, double> m_channelOffsets;
	std::map<int, bool> m_channelsEnabled;

	//External trigger input
	OscilloscopeChannel* m_extTrigChannel;
	std::vector<OscilloscopeChannel*> m_digitalChannels;
};

#endif
