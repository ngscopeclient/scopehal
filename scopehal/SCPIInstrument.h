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

#ifndef SCPIInstrument_h
#define SCPIInstrument_h

/**
	@brief SCPI transport information (type and connectionString)
 */
struct SCPITransportInfo
{
    SCPITransportType transportType;
    std::string connectionString;
};

/**
	@brief SCPI instrument model (model name and transport information)
 */
struct SCPIInstrumentModel
{
    std::string modelName;
    std::vector<SCPITransportInfo> supportedTransports;
};

/**
	@brief An SCPI-based oscilloscope
 */
class SCPIInstrument 	: public virtual Instrument
						, public virtual SCPIDevice
{
public:
	SCPIInstrument(SCPITransport* transport, bool identify = true);
	virtual ~SCPIInstrument();

	virtual std::string GetTransportConnectionString() override;
	virtual std::string GetTransportName() override;

	virtual std::string GetName() const override;
	virtual std::string GetVendor() const override;
	virtual std::string GetSerial() const override;
	virtual std::string GetDriverName() const =0;

	virtual void BackgroundProcessing() override;

	typedef std::vector<SCPIInstrumentModel> (*GetTransportsProcType)();
	static void DoAddDriverClass(std::string name, GetTransportsProcType proc);
	static std::vector<SCPIInstrumentModel> GetSupportedModels(std::string driver);
	static std::vector<SCPIInstrumentModel> GetDriverSupportedModels();

protected:
	typedef std::map<std::string, GetTransportsProcType > GetTransportMapType;
	static GetTransportMapType m_getTransportProcs;

protected:
	void DoSerializeConfiguration(YAML::Node& node, IDTable& table);
};

#endif
