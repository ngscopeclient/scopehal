/**
	@file
	@author Andrés MANELLI
	@brief Declaration of CANRenderer
 */

#include "scopeprotocols.h"
#include "../scopehal/scopehal.h"
#include "../scopehal/ChannelRenderer.h"
#include "../scopehal/TextRenderer.h"
#include "CANRenderer.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

CANRenderer::CANRenderer(OscilloscopeChannel* channel)
: TextRenderer(channel)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rendering

Gdk::Color CANRenderer::GetColor(int i)
{
	CANCapture* capture = dynamic_cast<CANCapture*>(m_channel->GetData());
	if(capture != NULL)
	{
		const CANSymbol& s = capture->m_samples[i].m_sample;

		if(s.m_stype == CANSymbol::TYPE_SOF)
			return m_standardColors[COLOR_CONTROL];
		else if(s.m_stype == CANSymbol::TYPE_SID)
			return m_standardColors[COLOR_ADDRESS];
		else if(s.m_stype == CANSymbol::TYPE_RTR)
			return m_standardColors[COLOR_CONTROL];
		else if(s.m_stype == CANSymbol::TYPE_IDE)
			return m_standardColors[COLOR_CONTROL];
		else if(s.m_stype == CANSymbol::TYPE_R0)
			return m_standardColors[COLOR_CONTROL];
		else if(s.m_stype == CANSymbol::TYPE_DLC)
			return m_standardColors[COLOR_CONTROL];
		else if(s.m_stype == CANSymbol::TYPE_DATA)
			return m_standardColors[COLOR_DATA];
		else if(s.m_stype == CANSymbol::TYPE_CRC)
			return m_standardColors[COLOR_CHECKSUM_OK];
		else
			return m_standardColors[COLOR_IDLE];
	}

	return m_standardColors[COLOR_ERROR];
}

string CANRenderer::GetText(int i)
{
	CANCapture* capture = dynamic_cast<CANCapture*>(m_channel->GetData());
	if(capture != NULL)
	{
		const CANSymbol& s = capture->m_samples[i].m_sample;

		char tmp[32];
		switch(s.m_stype)
		{
			case CANSymbol::TYPE_SOF:
				snprintf(tmp, sizeof(tmp), "SOF");
				break;
			case CANSymbol::TYPE_SID:
				{
					uint16_t sid = 0;
					sid += s.m_data[1];
					sid <<= 8;
					sid += s.m_data[0];
					snprintf(tmp, sizeof(tmp), "SID: %02x", sid);
				}
				break;
			case CANSymbol::TYPE_RTR:
				snprintf(tmp, sizeof(tmp), "RTR: %u", s.m_data[0]);
				break;
			case CANSymbol::TYPE_IDE:
				snprintf(tmp, sizeof(tmp), "IDE: %u", s.m_data[0]);
				break;
			case CANSymbol::TYPE_R0:
				snprintf(tmp, sizeof(tmp), "R0: %u", s.m_data[0]);
				break;
			case CANSymbol::TYPE_DLC:
				snprintf(tmp, sizeof(tmp), "DLC: %u", s.m_data[0]);
				break;
			case CANSymbol::TYPE_DATA:
				snprintf(tmp, sizeof(tmp), "%02x", s.m_data[0]);
				break;
			case CANSymbol::TYPE_CRC:
				{
					uint16_t crc = 0;
					crc += s.m_data[1];
					crc <<= 8;
					crc += s.m_data[0];
					snprintf(tmp, sizeof(tmp), "CRC: %02x", crc);
				}
				break;
			default:
				snprintf(tmp, sizeof(tmp), "ERR");
				break;
		}
		return string(tmp);
	}
	return "";
}
