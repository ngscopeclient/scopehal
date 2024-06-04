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
	@author Andrew D. Zonenberg
	@brief Declaration of VNA
 */

#ifndef SCPIVNA_h
#define SCPIVNA_h

/**
	@brief Generic representation of a vector network analyzer
 */
class SCPIVNA : public virtual SCPIOscilloscope
{
public:
	SCPIVNA();
	virtual ~SCPIVNA();

	virtual unsigned int GetInstrumentTypes() const override;
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) const override;

	//Channel configuration
	virtual bool IsChannelEnabled(size_t i) override;
	virtual void EnableChannel(size_t i) override;
	virtual void DisableChannel(size_t i) override;
	virtual OscilloscopeChannel::CouplingType GetChannelCoupling(size_t i) override;
	virtual void SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type) override;
	virtual std::vector<OscilloscopeChannel::CouplingType> GetAvailableCouplings(size_t i) override;
	virtual double GetChannelAttenuation(size_t i) override;
	virtual void SetChannelAttenuation(size_t i, double atten) override;
	virtual unsigned int GetChannelBandwidthLimit(size_t i) override;
	virtual void SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz) override;
	virtual float GetChannelVoltageRange(size_t i, size_t stream) override;
	virtual void SetChannelVoltageRange(size_t i, size_t stream, float range) override;
	virtual float GetChannelOffset(size_t i, size_t stream) override;
	virtual void SetChannelOffset(size_t i, size_t stream, float offset) override;

	//Timebase
	virtual std::set<InterleaveConflict> GetInterleaveConflicts() override;
	virtual std::vector<uint64_t> GetSampleRatesInterleaved() override;
	virtual std::vector<uint64_t> GetSampleDepthsInterleaved() override;
	virtual void SetTriggerOffset(int64_t offset) override;
	virtual int64_t GetTriggerOffset() override;
	virtual bool IsInterleaving() override;
	virtual bool SetInterleaving(bool combine) override;
	virtual std::vector<uint64_t> GetSampleRatesNonInterleaved() override;
	virtual uint64_t GetSampleRate() override;
	virtual void SetSampleRate(uint64_t rate) override;

	virtual bool HasFrequencyControls() override;
	virtual bool HasTimebaseControls() override;

protected:
	std::map<std::pair<size_t, size_t>, float> m_channelVoltageRange;
	std::map<std::pair<size_t, size_t>, float> m_channelOffset;

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Dynamic creation
public:
	typedef std::shared_ptr<SCPIVNA> (*VNACreateProcType)(SCPITransport*);
	static void DoAddDriverClass(std::string name, VNACreateProcType proc);

	static void EnumDrivers(std::vector<std::string>& names);
	static std::shared_ptr<SCPIVNA> CreateVNA(std::string driver, SCPITransport* transport);

	//Class enumeration
	typedef std::map< std::string, VNACreateProcType > VNACreateMapType;
	static VNACreateMapType m_vnacreateprocs;
};

#define VNA_INITPROC(T) \
	static std::shared_ptr<SCPIVNA> CreateInstance(SCPITransport* transport) \
	{	return std::make_shared<T>(transport); } \
	virtual std::string GetDriverName() const override \
	{ return GetDriverNameInternal(); }

#define AddVNADriverClass(T) SCPIVNA::DoAddDriverClass(T::GetDriverNameInternal(), T::CreateInstance)

#endif
