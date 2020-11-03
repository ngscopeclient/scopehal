/**
	@file
	@author Andrés MANELLI
	@brief Declaration of CANDecoder
 */

#ifndef CANDecoder_h
#define CANDecoder_h

#include "PacketDecoder.h"

class CANSymbol
{
public:
	enum stype
	{
		TYPE_SOF,
		TYPE_ID,
		TYPE_RTR,
		TYPE_R0,
		TYPE_FD,
		TYPE_DLC,
		TYPE_DATA,
		TYPE_CRC_OK,
		TYPE_CRC_BAD,
		TYPE_CRC_DELIM,
		TYPE_ACK,
		TYPE_ACK_DELIM,
		TYPE_EOF
	};

	CANSymbol()
	{}

	CANSymbol(stype t, uint32_t data)
	 : m_stype(t)
	 , m_data(data)
	{
	}

	stype m_stype;
	uint32_t m_data;

	bool operator== (const CANSymbol& s) const
	{
		return (m_stype == s.m_stype) && (m_data == s.m_data);
	}
};

typedef Waveform<CANSymbol> CANWaveform;

class CANDecoder : public PacketDecoder
{
public:
	CANDecoder(const std::string& color);

	virtual std::string GetText(int i);
	virtual Gdk::Color GetColor(int i);

	virtual void Refresh();
	virtual bool NeedsConfig();

	static std::string GetProtocolName();
	virtual void SetDefaultName();

	std::vector<std::string> GetHeaders();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream);

	PROTOCOL_DECODER_INITPROC(CANDecoder)

protected:
	std::string m_baudrateName;
};

#endif
