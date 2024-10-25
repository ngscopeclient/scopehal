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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Declaration of MultimeterChannel
	@ingroup datamodel
 */
#ifndef MultimeterChannel_h
#define MultimeterChannel_h

/**
	@brief A single channel of a multimeter

	@ingroup datamodel
 */
class MultimeterChannel : public InstrumentChannel
{
public:

	MultimeterChannel(
		Multimeter* parent,
		const std::string& hwname,
		const std::string& color = "#808080",
		size_t index = 0);

	virtual ~MultimeterChannel();

	///@brief Return the Multimeter this channel is attached to
	Multimeter* GetMeter()
	{ return dynamic_cast<Multimeter*>(m_instrument); }

	void Update();

	///@brief Return the value of our primary measurement
	float GetPrimaryValue()
	{ return GetScalarValue(m_primaryStream); }

	///@brief Return the value of our secondary measurement, if applicable
	float GetSecondaryValue()
	{ return GetScalarValue(m_secondaryStream); }

	virtual PhysicalConnector GetPhysicalConnector() override;

protected:

	///@brief Index of our primary output
	size_t m_primaryStream;

	///@brief Index of our secondary output
	size_t m_secondaryStream;

};

#endif
