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
	@brief Declaration of CSVStreamInstrument
	@ingroup miscdrivers
 */

#ifndef CSVStreamInstrument_h
#define CSVStreamInstrument_h

/**
	@brief A miscellaneous instrument which streams scalar data over CSV

	Typically used to plot real time system state (voltages, temperatures, ADC values, etc) from a microcontroller over
	a UART or SWO trace interface.

	The instrument protocol is a unidirectional stream of line oriented comma-separated-value (CSV) rather than SCPI.

	At any time, the instrument may send lines with one or more of the following formats, separated by \n characters.
	Lines not starting with these magic keywords are ignored.

	* CSV-NAME,ch1name,ch2name, ... : assign human readable names to channels
	* CSV-UNIT,V,A, ... : specify unit associated with each channel
	* CSV-DATA,1.23,3.14, ... : specify latest measurement value for each channel.
	It is not possible to perform partial updates of a single channel without updating the others.

	@ingroup miscdrivers
 */
class CSVStreamInstrument
	: public virtual SCPIMiscInstrument
{
public:
	CSVStreamInstrument(SCPITransport* transport);
	virtual ~CSVStreamInstrument();

	//Device information
	virtual uint32_t GetInstrumentTypes() const override;
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) const override;

	//Acquisition
	virtual bool AcquireData() override;

protected:

	/**
		@brief Validate instrument and channel configuration from a save file
	 */
	void DoPreLoadConfiguration(int version, const YAML::Node& node, IDTable& idmap, ConfigWarningList& list);

public:
	static std::string GetDriverNameInternal();
	MISC_INITPROC(CSVStreamInstrument);
};

#endif
