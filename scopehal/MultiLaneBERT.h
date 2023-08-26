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

#ifndef MultiLaneBERT_h
#define MultiLaneBERT_h

/**
	@brief A MultiLANE BERT accessed via scopehal-mlbert-bridge
 */
class MultiLaneBERT
	: public virtual SCPIBERT
	, public virtual SCPIDevice
{
public:
	MultiLaneBERT(SCPITransport* transport);
	virtual ~MultiLaneBERT();

	virtual bool AcquireData() override;

	//Device information
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) override;

	//Pattern configuration
	virtual Pattern GetTxPattern(size_t i);
	virtual void SetTxPattern(size_t i, Pattern pattern);
	virtual Pattern GetRxPattern(size_t i);
	virtual void SetRxPattern(size_t i, Pattern pattern);
	virtual bool GetTxInvert(size_t i);
	virtual bool GetRxInvert(size_t i);
	virtual void SetTxInvert(size_t i, bool invert);
	virtual void SetRxInvert(size_t i, bool invert);
	virtual std::vector<Pattern> GetAvailableTxPatterns(size_t i);
	virtual std::vector<Pattern> GetAvailableRxPatterns(size_t i);

protected:

	int m_rxChannelBase;

	//Cached settings
	Pattern m_txPattern[4];
	Pattern m_rxPattern[4];
	bool m_txInvert[4];
	bool m_rxInvert[4];

public:
	static std::string GetDriverNameInternal();
	BERT_INITPROC(MultiLaneBERT)
};

#endif
