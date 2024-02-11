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

#ifndef SiglentLoad_h
#define SiglentLoad_h

/**
	@brief Siglent electronic load

	So far only series available is SDL1000X-E, base X should be the same (just higher resolution).
 */
class SiglentLoad
	: public virtual SCPILoad
{
public:
	SiglentLoad(SCPITransport* transport);
	virtual ~SiglentLoad();

	//Instrument
	virtual unsigned int GetInstrumentTypes() const override;
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) const override;

	//Load
	virtual LoadMode GetLoadMode(size_t channel) override;
	virtual void SetLoadMode(size_t channel, LoadMode mode) override;
	virtual std::vector<float> GetLoadCurrentRanges(size_t channel) override;
	virtual size_t GetLoadCurrentRange(size_t channel) override;
	virtual std::vector<float> GetLoadVoltageRanges(size_t channel) override;
	virtual size_t GetLoadVoltageRange(size_t channel) override;
	virtual bool GetLoadActive(size_t channel) override;
	virtual void SetLoadActive(size_t channel, bool active) override;
	virtual void SetLoadVoltageRange(size_t channel, size_t rangeIndex) override;
	virtual void SetLoadCurrentRange(size_t channel, size_t rangeIndex) override;

	virtual float GetLoadSetPoint(size_t channel) override;
	virtual void SetLoadSetPoint(size_t channel, float target) override;

	//TODO: FlushConfigCache should get pulled up from Oscilloscope into Load

public:
	static std::string GetDriverNameInternal();
	LOAD_INITPROC(SiglentLoad)

protected:
	LoadMode GetLoadModeUncached(size_t channel);

	virtual float GetLoadVoltageActual(size_t channel) override;
	virtual float GetLoadCurrentActual(size_t channel) override;
	virtual float GetLoadSetPointActual(size_t channel);

	//Cache config
	LoadMode m_modeCached;
	float m_setPointCached;
};

#endif
