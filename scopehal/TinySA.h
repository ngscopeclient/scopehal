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
	@author Frederic Borry
	@brief Declaration of TinySA

	@ingroup scopedrivers
 */

#ifndef TinySA_h
#define TinySA_h

/**
	@brief Driver for TinySA and TinySA Ultra Spectrum Analizers
	@ingroup scopedrivers

	TinySA and TinySA Ultra are hobyist low-cost Spectrum Analizer designed by Erik Kaashoek: https://tinysa.org/
	They can be connected to a PC via a USB COM port.

 */
class TinySA
	: public virtual SCPISA
{
public:
	TinySA(SCPITransport* transport);
	virtual ~TinySA();

	//not copyable or assignable
	TinySA(const TinySA& rhs) =delete;
	TinySA& operator=(const TinySA& rhs) =delete;


public:

	//Channel configuration

	virtual void FlushConfigCache() override;

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


	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Spectrum analyzer configuration

	virtual std::vector<uint64_t> GetSampleDepthsNonInterleaved() override;
	virtual uint64_t GetSampleDepth() override;
	virtual void SetSampleDepth(uint64_t depth) override;
	virtual void SetSpan(int64_t span) override;
	virtual int64_t GetSpan() override;
	virtual void SetCenterFrequency(size_t channel, int64_t freq) override;
	virtual int64_t GetCenterFrequency(size_t channel) override;

	//TODO: Sweep configuration ?
	virtual void SetResolutionBandwidth(int64_t rbw);
	virtual int64_t GetResolutionBandwidth() override;

protected:
	// Make sure several request don't collide before we received the corresponding response
	std::recursive_mutex m_transportMutex;

	std::string ConverseSingle(const std::string commandString);
	size_t ConverseMultiple(const std::string commandString, std::vector<std::string> &readLines);
	std::string ConverseString(const std::string commandString);
	size_t ConverseBinary(const std::string commandString, std::vector<uint8_t> &data);

	//config cache
	std::string GetChannelColor(size_t i);

	bool m_triggerArmed;
	bool m_triggerOneShot;

	bool m_sampleDepthValid;
	int64_t m_sampleDepth;
	bool m_rbwValid;
	int64_t m_rbw;

	inline static const std::string TRAILER_STRING = "ch> ";
	inline static const size_t TRAILER_STRING_LENGTH = TRAILER_STRING.size();
	static const size_t MAX_RESPONSE_SIZE = 100*1024;

public:
	static std::string GetDriverNameInternal();
	OSCILLOSCOPE_INITPROC(TinySA)
};

#endif
