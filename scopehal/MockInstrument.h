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
	@brief Declaration of MockInstrument

	@ingroup psudrivers
 */

#ifndef MockInstrument_h
#define MockInstrument_h

/**
	@brief Base class for simulated instruments
 */
class MockInstrument:  public virtual SCPIInstrument
{
public:
	MockInstrument(
		const std::string& name,
		const std::string& vendor,
		const std::string& serial,
		const std::string& transport,
		const std::string& driver,
		const std::string& args
	);
	virtual ~MockInstrument();

	//Device information
	virtual bool IsOffline() override;

	virtual std::string GetTransportConnectionString() override;
	virtual void SetTransportConnectionString(const std::string& args);
	virtual std::string GetTransportName() override;

	virtual std::string GetName() const override;
	virtual std::string GetVendor() const override;
	virtual std::string GetSerial() const override;

	// Serialization
	void DoSerializeConfiguration(YAML::Node& node, IDTable& table);
	void ClearWarnings(int version, const YAML::Node& node, IDTable& idmap, ConfigWarningList& warnings);

	// SCPI
	virtual void BackgroundProcessing() override {}

protected:

	//standard *IDN? fields
	std::string m_name;
	std::string m_vendor;
	std::string m_serial;
	std::string m_fwVersion;

	//Simulated transport information
	std::string m_transportName;
	std::string m_driver;
	std::string m_args;

public:
	virtual std::string GetDriverName() const override
	{ return m_driver; }
};

#endif
