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
	{ return dynamic_cast<BERT*>(m_instrument); }

	void SetPattern(BERT::Pattern pattern)
	{ GetBERT()->SetTxPattern(GetIndex(), pattern); }

	BERT::Pattern GetPattern()
	{ return GetBERT()->GetTxPattern(GetIndex()); }

	std::vector<BERT::Pattern> GetAvailablePatterns()
	{ return GetBERT()->GetAvailableTxPatterns(GetIndex()); }

	bool GetInvert()
	{ return GetBERT()->GetTxInvert(GetIndex()); }

	void SetInvert(bool invert)
	{ GetBERT()->SetTxInvert(GetIndex(), invert); }

	std::vector<float> GetAvailableDriveStrengths()
	{ return GetBERT()->GetAvailableTxDriveStrengths(GetIndex()); }

	float GetDriveStrength()
	{ return GetBERT()->GetTxDriveStrength(GetIndex()); }

	void SetDriveStrength(float drive)
	{ GetBERT()->SetTxDriveStrength(GetIndex(), drive); }

	bool GetEnable()
	{ return GetBERT()->GetTxEnable(GetIndex()); }

	void Enable(bool b)
	{ GetBERT()->SetTxEnable(GetIndex(), b); }

	float GetPreCursor()
	{ return GetBERT()->GetTxPreCursor(GetIndex()); }

	void SetPreCursor(float f)
	{ GetBERT()->SetTxPreCursor(GetIndex(), f); }

	float GetPostCursor()
	{ return GetBERT()->GetTxPostCursor(GetIndex()); }

	void SetPostCursor(float f)
	{ GetBERT()->SetTxPostCursor(GetIndex(), f); }

	int64_t GetDataRate()
	{ return GetBERT()->GetDataRate(GetIndex()); }

	void SetDataRate(int64_t rate)
	{ GetBERT()->SetDataRate(GetIndex(), rate); }

	virtual PhysicalConnector GetPhysicalConnector() override;
};

#endif
