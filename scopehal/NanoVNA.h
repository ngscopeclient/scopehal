/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
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
	@author Frederic Borry
	@brief Declaration of NanoVNA
	@ingroup vnadrivers
 */

#ifndef NanoVNA_h
#define NanoVNA_h

class EdgeTrigger;

/**
	@brief NanoVNA - driver for talking to a NanoVNA using the NanoVNA 5 software
	@ingroup vnadrivers
 */
class NanoVNA : public virtual SCPIVNA, public virtual CommandLineDriver
{
public:
	NanoVNA(SCPITransport* transport);
	virtual ~NanoVNA();

	//not copyable or assignable
	NanoVNA(const NanoVNA& rhs) =delete;
	NanoVNA& operator=(const NanoVNA& rhs) =delete;

public:

	//Channel configuration

	//Triggering
	virtual OscilloscopeChannel* GetExternalTrigger() override;
	virtual Oscilloscope::TriggerMode PollTrigger() override;
	virtual bool AcquireData() override;
	virtual void Start() override;
	virtual void StartSingleTrigger() override;
	virtual void Stop() override;
	virtual void ForceTrigger() override;
	virtual bool IsTriggerArmed() override;
	virtual void PushTrigger() override;
	virtual void PullTrigger() override;

	//Timebase
	virtual std::vector<uint64_t> GetSampleDepthsNonInterleaved() override;
	virtual uint64_t GetSampleDepth() override;
	virtual void SetSampleDepth(uint64_t depth) override;
	// Sweep configuration
	virtual void SetSpan(int64_t span) override;
	virtual int64_t GetSpan() override;
	virtual void SetCenterFrequency(size_t channel, int64_t freq) override;
	virtual int64_t GetCenterFrequency(size_t channel) override;
	// Rbw
	virtual int64_t GetResolutionBandwidth() override;
	virtual void SetResolutionBandwidth(int64_t rbw) override;

protected:
	// Device communication methods and members
	enum Model {
		MODEL_UNKNOWN,
		MODEL_NANOVNA,
		MODEL_NANOVNA_D,
		MODEL_NANOVNA_F_DEEPELEC,
		MODEL_NANOVNA_F,
		MODEL_NANOVNA_H,
		MODEL_NANOVNA_H4,
		MODEL_NANOVNA_F_V2,
		MODEL_NANOVNA_V2
	};

	void SendBandwidthValue(int64_t bandwidth);

	std::string GetChannelColor(size_t i);

	bool m_triggerArmed = false;
	bool m_triggerOneShot = false;

	int64_t m_sampleDepth = 0;
	size_t m_maxDeviceSampleDepth = 0;
	int64_t m_rbw = 0;
	std::map<int64_t,int64_t> m_rbwValues;

	Model m_nanoVNAModel = MODEL_UNKNOWN;

	// Span control
	int64_t m_sweepStart;
	int64_t m_sweepStop;

	int64_t m_freqMax;
	int64_t m_freqMin;

public:
	static std::string GetDriverNameInternal();
	static std::vector<SCPIInstrumentModel> GetDriverSupportedModels()
	{
		return {
	#ifdef _WIN32
        {"NanoVNA", {{ SCPITransportType::TRANSPORT_UART, "COM<x>:115200:DTR" }}}
	#else
        {"NanoVNA", {{ SCPITransportType::TRANSPORT_UART, "/dev/ttyUSB<x>:115200:DTR" }}}
	#endif
        };
	}
	VNA_INITPROC(NanoVNA)
};

#endif
