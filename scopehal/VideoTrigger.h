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

/**
	@file
	@author Hansem Ro
	@brief Declaration of VideoTrigger 
 */
#ifndef VideoTrigger_h
#define VideoTrigger_h

/**
	@brief Trigger when a signal stops toggling for some amount of time
 */
class VideoTrigger : public Trigger
{
public:
	VideoTrigger(Oscilloscope* scope);
	virtual ~VideoTrigger();

	enum StandardType
	{
		NTSC,
		PAL,
		P720L50,
		P720L60,
		P1080L50,
		P1080L60,
		I1080L50,
		I1080L60,
		CUSTOM
	};

	void SetStandard(StandardType type)
	{ m_parameters[m_standardname].SetIntVal(type); }

	StandardType GetStandard()
	{ return (StandardType) m_parameters[m_standardname].GetIntVal(); }

	enum SyncMode
	{
		ANY,
		SELECT
	};

	void SetSyncMode(SyncMode mode)
	{ m_parameters[m_syncmode].SetIntVal(mode); }

	SyncMode GetSyncMode()
	{ return (SyncMode) m_parameters[m_syncmode].GetIntVal(); }

	void SetLine(int64_t n)
	{ m_parameters[m_linename].SetIntVal(n); }

	int64_t GetLine()
	{ return m_parameters[m_linename].GetIntVal(); }

	void SetField(int64_t n)
	{ m_parameters[m_fieldname].SetIntVal(n); }

	int64_t GetField()
	{ return m_parameters[m_fieldname].GetIntVal(); }

	enum FrameRate
	{
		FRAMERATE_25HZ,
		FRAMERATE_30HZ,
		FRAMERATE_50HZ,
		FRAMERATE_60HZ
	};

	void SetFrameRate(FrameRate rate)
	{ m_parameters[m_framerate].SetIntVal(rate); }

	FrameRate GetFrameRate()
	{ return (FrameRate) m_parameters[m_framerate].GetIntVal(); }

	void SetInterlace(int64_t n)
	{ m_parameters[m_interlace].SetIntVal(n); }

	int64_t GetInterlace()
	{ return m_parameters[m_interlace].GetIntVal(); }

	void SetLineCount(int64_t n)
	{ m_parameters[m_linecount].SetIntVal(n); }

	int64_t GetLineCount()
	{ return m_parameters[m_linecount].GetIntVal(); }

	void SetFieldCount(int64_t n)
	{ m_parameters[m_fieldcount].SetIntVal(n); }

	int64_t GetFieldCount()
	{ return m_parameters[m_fieldcount].GetIntVal(); }

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream);

	static std::string GetTriggerName();
	TRIGGER_INITPROC(VideoTrigger);

protected:
	std::string m_standardname;
	std::string m_syncmode;
	std::string m_linename;
	std::string m_fieldname;
	std::string m_framerate;
	std::string m_interlace;
	std::string m_linecount;
	std::string m_fieldcount;
};

#endif
