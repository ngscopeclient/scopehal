/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Declaration of SerialTrigger
 */
#ifndef SerialTrigger_h
#define SerialTrigger_h

/**
	@brief Abstract base class for serial protocol triggers with pattern matching
 */
class SerialTrigger : public Trigger
{
public:
	SerialTrigger(Oscilloscope* scope);
	virtual ~SerialTrigger();

	enum Radix
	{
		RADIX_ASCII,
		RADIX_HEX,
		RADIX_BINARY
	};

	void SetCondition(Condition cond)
	{ m_condition.SetIntVal(cond); }

	Condition GetCondition()
	{ return (Condition) m_condition.GetIntVal(); }

	void SetRadix(Radix rad)
	{ m_radix.SetIntVal(rad); }

	Radix GetRadix()
	{ return (Radix) m_radix.GetIntVal(); }

	void SetPatterns(const std::string& p1, const std::string& p2, bool ignore_p2);

	std::string GetPattern1()
	{ return FormatPattern(m_pattern.ToString()); }

	std::string GetPattern2()
	{ return FormatPattern(m_pattern2.ToString()); }

protected:

	std::string FormatPattern(const std::string& str);

	FilterParameter m_radix;
	FilterParameter m_condition;
	FilterParameter m_pattern;
	FilterParameter m_pattern2;
};

#endif
