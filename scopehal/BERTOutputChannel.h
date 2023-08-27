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

#ifndef BERTOutputChannel_h
#define BERTOutputChannel_h

#include "BERT.h"

/**
	@brief A pattern generator channel of a BERT
 */
class BERTOutputChannel : public InstrumentChannel
{
public:

	BERTOutputChannel(
		const std::string& hwname,
		BERT* bert,
		const std::string& color = "#808080",
		size_t index = 0);

	virtual ~BERTOutputChannel();

	virtual void Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue) override;
	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	BERT* GetBERT() const
	{ return m_bert; }

	void SetPattern(BERT::Pattern pattern)
	{ m_bert->SetTxPattern(GetIndex(), pattern); }

	BERT::Pattern GetPattern()
	{ return m_bert->GetTxPattern(GetIndex()); }

	std::vector<BERT::Pattern> GetAvailablePatterns()
	{ return m_bert->GetAvailableTxPatterns(GetIndex()); }

	bool GetInvert()
	{ return m_bert->GetTxInvert(GetIndex()); }

	void SetInvert(bool invert)
	{ m_bert->SetTxInvert(GetIndex(), invert); }

	std::vector<float> GetAvailableDriveStrengths()
	{ return m_bert->GetAvailableTxDriveStrengths(GetIndex()); }

	float GetDriveStrength()
	{ return m_bert->GetTxDriveStrength(GetIndex()); }

	void SetDriveStrength(float drive)
	{ m_bert->SetTxDriveStrength(GetIndex(), drive); }

	bool GetEnable()
	{ return m_bert->GetTxEnable(GetIndex()); }

	void Enable(bool b)
	{ m_bert->SetTxEnable(GetIndex(), b); }

	float GetPreCursor()
	{ return m_bert->GetTxPreCursor(GetIndex()); }

	void SetPreCursor(float f)
	{ m_bert->SetTxPreCursor(GetIndex(), f); }

	float GetPostCursor()
	{ return m_bert->GetTxPostCursor(GetIndex()); }

	void SetPostCursor(float f)
	{ m_bert->SetTxPostCursor(GetIndex(), f); }

protected:
	BERT* m_bert;
};

#endif
