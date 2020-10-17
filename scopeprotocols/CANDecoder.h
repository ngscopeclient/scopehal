/**
	@file
	@author Andrés MANELLI
	@brief Declaration of CANDecoder
 */

#ifndef CANDecoder_h
#define CANDecoder_h

class CANSymbol
{
public:
	enum stype
	{
		TYPE_SOF,
		TYPE_SID,
		TYPE_RTR,
		TYPE_IDE,
		TYPE_R0,
		TYPE_DLC,
		TYPE_DATA,
		TYPE_CRC,
		TYPE_IDLE
	};

	CANSymbol()
	{}

	CANSymbol(stype t, uint8_t *data, size_t size)
	 : m_stype(t)
	{
		for (uint8_t i = 0; i < size; i++)
		{
			m_data.push_back(data[i]);
		}
	}

	stype m_stype;
	std::vector<uint8_t> m_data;

	bool operator== (const CANSymbol& s) const
	{
		return (m_stype == s.m_stype) && (m_data == s.m_data);
	}
};

typedef Waveform<CANSymbol> CANWaveform;

class CANDecoder : public Filter
{
public:
	CANDecoder(const std::string& color);

	virtual std::string GetText(int i);
	virtual Gdk::Color GetColor(int i);

	virtual void Refresh();
	virtual bool NeedsConfig();

	static std::string GetProtocolName();
	virtual void SetDefaultName();

	virtual bool ValidateChannel(size_t i, StreamDescriptor stream);

	PROTOCOL_DECODER_INITPROC(CANDecoder)

protected:
	std::string m_tq;
	std::string m_bs1, m_bs2;
};

#endif
