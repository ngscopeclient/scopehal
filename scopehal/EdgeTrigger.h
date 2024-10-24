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
	@brief Declaration of EdgeTrigger
	@ingroup triggers
 */
#ifndef EdgeTrigger_h
#define EdgeTrigger_h

/**
	@brief Simple edge trigger
	@ingroup triggers
 */
class EdgeTrigger : public Trigger
{
public:
	EdgeTrigger(Oscilloscope* scope);
	virtual ~EdgeTrigger();

	///@brief Types of edges to trogger on
	enum EdgeType
	{
		///@brief Low to high transition
		EDGE_RISING,

		///@brief High to low transition
		EDGE_FALLING,

		///@brief Either rising or falling transition
		EDGE_ANY,

		/**
			@brief Trigger on rising edge, then falling, then rising again, and so on

			Not widely supported, only known to be implemented by a handful of Agilent models
		 */
		EDGE_ALTERNATING
	};

	/**
		@brief Set the type of the edge to trigger on

		@param type	Edge type
	 */
	void SetType(EdgeType type)
	{ m_edgetype.SetIntVal(type); }

	///@brief Gets the currently selected edge type
	EdgeType GetType()
	{ return (EdgeType) m_edgetype.GetIntVal(); }

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream) override;

	static std::string GetTriggerName();
	TRIGGER_INITPROC(EdgeTrigger);

protected:
	FilterParameter& m_edgetype;
};

#endif
