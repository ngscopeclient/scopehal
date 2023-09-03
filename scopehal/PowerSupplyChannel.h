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

#ifndef PowerSupplyChannel_h
#define PowerSupplyChannel_h

/**
	@brief A single channel of a power supply
 */
class PowerSupplyChannel : public InstrumentChannel
{
public:

	PowerSupplyChannel(
		const std::string& hwname,
		PowerSupply* powerSupply,
		const std::string& color = "#808080",
		size_t index = 0);

	virtual ~PowerSupplyChannel();

	//Well defined stream IDs used by PowerSupplyChannel
	enum StreamIndexes
	{
		STREAM_VOLTAGE_MEASURED,
		STREAM_VOLTAGE_SET_POINT,
		STREAM_CURRENT_MEASURED,
		STREAM_CURRENT_SET_POINT
	};

	float GetVoltageMeasured()
	{ return GetScalarValue(STREAM_VOLTAGE_MEASURED); }

	float GetVoltageSetPoint()
	{ return GetScalarValue(STREAM_VOLTAGE_SET_POINT); }

	float GetCurrentMeasured()
	{ return GetScalarValue(STREAM_CURRENT_MEASURED); }

	float GetCurrentSetPoint()
	{ return GetScalarValue(STREAM_CURRENT_SET_POINT); }

	virtual void Refresh(vk::raii::CommandBuffer& cmdBuf, std::shared_ptr<QueueHandle> queue) override;
	virtual bool ValidateChannel(size_t i, StreamDescriptor stream);

	virtual PhysicalConnector GetPhysicalConnector() override;

protected:
	PowerSupply* m_powerSupply;
};

#endif
