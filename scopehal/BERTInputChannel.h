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

#ifndef BERTInputChannel_h
#define BERTInputChannel_h

class BERT;

/**
	@brief A pattern checker channel of a BERT

	Derived from OscilloscopeChannel because we can output time domain bathtub curves etc
 */
class BERTInputChannel : public OscilloscopeChannel
{
public:

	BERTInputChannel(
		const std::string& hwname,
		BERT* bert,
		const std::string& color = "#808080",
		size_t index = 0);

	virtual ~BERTInputChannel();

	virtual void Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue) override;
	virtual bool ValidateChannel(size_t i, StreamDescriptor stream);

	BERT* GetBERT() const
	{ return m_bert; }

	bool GetInvert()
	{ return m_bert->GetRxInvert(GetIndex()); }

	void SetInvert(bool invert)
	{ m_bert->SetRxInvert(GetIndex(), invert); }

	bool GetCdrLockState()
	{ return m_bert->GetRxCdrLockState(GetIndex()); }

	void SetPattern(BERT::Pattern pattern)
	{ m_bert->SetRxPattern(GetIndex(), pattern); }

	BERT::Pattern GetPattern()
	{ return m_bert->GetRxPattern(GetIndex()); }

	std::vector<BERT::Pattern> GetAvailablePatterns()
	{ return m_bert->GetAvailableRxPatterns(GetIndex()); }

	enum StreamIDs
	{
		STREAM_HBATHTUB = 0,
		STREAM_EYE = 1,
		STREAM_BER
	};

	StreamDescriptor GetHBathtubStream()
	{ return StreamDescriptor(this, STREAM_HBATHTUB); }

	StreamDescriptor GetEyeStream()
	{ return StreamDescriptor(this, STREAM_EYE); }

	bool HasCTLE()
	{ return m_bert->HasRxCTLE(); }

	std::vector<float> GetCTLEGainSteps()
	{ return m_bert->GetRxCTLEGainSteps(); }

	size_t GetCTLEGainStep()
	{ return m_bert->GetRxCTLEGainStep(GetIndex()); }

	void SetCTLEGainStep(size_t step)
	{ m_bert->SetRxCTLEGainStep(GetIndex(), step); }

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Vertical scaling and stream management

public:
	virtual void ClearStreams() override;
	virtual size_t AddStream(Unit yunit, const std::string& name, Stream::StreamType stype, uint8_t flags = 0) override;

	virtual float GetVoltageRange(size_t stream) override;
	virtual void SetVoltageRange(float range, size_t stream) override;

	virtual float GetOffset(size_t stream) override;
	virtual void SetOffset(float offset, size_t stream) override;

protected:
	std::vector<float> m_ranges;
	std::vector<float> m_offsets;


protected:
	BERT* m_bert;
};

#endif
