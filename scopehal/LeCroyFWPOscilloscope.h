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

#ifndef LeCroyFWPOscilloscope_h
#define LeCroyFWPOscilloscope_h

#include "LeCroyOscilloscope.h"

/**
	@brief A Teledyne LeCroy oscilloscope using the FastWavePort interface for download instead of SCPI

	Requires the instrument to have the XDEV option installed, and scopehal-fwp-bridge running
 */
class LeCroyFWPOscilloscope : public LeCroyOscilloscope
{
public:
	LeCroyFWPOscilloscope(SCPITransport* transport);
	virtual ~LeCroyFWPOscilloscope();

	//not copyable or assignable
	LeCroyFWPOscilloscope(const LeCroyFWPOscilloscope& rhs) =delete;
	LeCroyFWPOscilloscope& operator=(const LeCroyFWPOscilloscope& rhs) =delete;

	virtual Oscilloscope::TriggerMode PollTrigger() override;
	virtual bool AcquireData() override;
	virtual void Start() override;

	virtual void EnableChannel(size_t i) override;
	virtual void DisableChannel(size_t i) override;

	virtual std::vector<uint64_t> GetSampleDepthsNonInterleaved() override;
	virtual std::vector<uint64_t> GetSampleDepthsInterleaved() override;

protected:
	void SendEnableMask();

	///@brief Indicates we're operating in fallback mode (FWP wasn't available for some reason)
	bool m_fallback;

	Socket m_socket;

public:
	static std::string GetDriverNameInternal();
	OSCILLOSCOPE_INITPROC(LeCroyFWPOscilloscope)
};

#endif
