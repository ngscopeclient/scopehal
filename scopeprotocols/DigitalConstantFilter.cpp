/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
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

#include "../scopehal/scopehal.h"
#include "DigitalConstantFilter.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DigitalConstantFilter::DigitalConstantFilter(const string& color)
	: Filter(color, CAT_GENERATION)
	, m_value(m_parameters["Value"])
	, m_width(m_parameters["Width"])
{
	AddStream(Unit(Unit::UNIT_COUNTS), "data", Stream::STREAM_TYPE_DIGITAL_SCALAR);

	m_value = FilterParameter(FilterParameter::TYPE_BOOL, Unit(Unit::UNIT_HEXNUM));
	m_value.SetIntVal(0);

	m_width = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_width.SetIntVal(1);
	m_width.signal_changed().connect(sigc::mem_fun(*this, &DigitalConstantFilter::OnWidthChanged));
	OnWidthChanged();

	SetData(nullptr, 0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string DigitalConstantFilter::GetProtocolName()
{
	return "Digital Constant";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void DigitalConstantFilter::OnWidthChanged()
{
	auto width = m_width.GetIntVal();
	if(width < 1)
		width = 1;
	if(width > 64)
		width = 64;

	if( (width == 1) && (m_value.GetType() != FilterParameter::TYPE_BOOL))
		m_value = FilterParameter(FilterParameter::TYPE_BOOL, Unit(Unit::UNIT_HEXNUM));
	else if( (width != 1) && (m_value.GetType() != FilterParameter::TYPE_INT))
		m_value = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_HEXNUM));

	m_streams[0].m_digitalValueWidth = width;
}

void DigitalConstantFilter::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	m_streams[0].m_digitalValue = m_value.GetIntVal();
}
