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

#ifndef BERTInputChannel_h
#define BERTInputChannel_h

class BERT;
#include "EyeMask.h"

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
	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

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

	int64_t GetScanDepth()
	{ return m_bert->GetScanDepth(GetIndex()); }

	std::vector<int64_t> GetScanDepths()
	{ return m_bert->GetScanDepths(GetIndex()); }

	void SetScanDepth(int64_t depth)
	{ m_bert->SetScanDepth(GetIndex(), depth); }

	enum StreamIDs
	{
		STREAM_HBATHTUB		= 0,
		STREAM_EYE 			= 1,
		STREAM_BER 			= 2,
		STREAM_MASKHITRATE	= 3
	};

	StreamDescriptor GetHBathtubStream()
	{ return StreamDescriptor(this, STREAM_HBATHTUB); }

	StreamDescriptor GetEyeStream()
	{ return StreamDescriptor(this, STREAM_EYE); }

	StreamDescriptor GetBERStream()
	{ return StreamDescriptor(this, STREAM_BER); }

	bool HasCTLE()
	{ return m_bert->HasRxCTLE(); }

	std::vector<float> GetCTLEGainSteps()
	{ return m_bert->GetRxCTLEGainSteps(); }

	size_t GetCTLEGainStep()
	{ return m_bert->GetRxCTLEGainStep(GetIndex()); }

	void SetCTLEGainStep(size_t step)
	{ m_bert->SetRxCTLEGainStep(GetIndex(), step); }

	void SetBERSamplingPoint(int64_t dx, float dy)
	{ m_bert->SetBERSamplingPoint(GetIndex(), dx, dy); }

	void GetBERSamplingPoint(int64_t& dx, float& dy)
	{ m_bert->GetBERSamplingPoint(GetIndex(), dx, dy); }

	std::string GetMaskFile()
	{ return m_maskFile; }

	void SetMaskFile(const std::string& fname);

	const EyeMask& GetMask()
	{ return m_mask; }

	virtual PhysicalConnector GetPhysicalConnector() override;

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

	std::string m_maskFile;
	EyeMask m_mask;

protected:
	BERT* m_bert;
};

#endif
