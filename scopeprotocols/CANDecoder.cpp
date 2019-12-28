/**
	@file
	@author Andrés MANELLI
	@brief Implementation of CANDecoder
 */

#include "../scopehal/scopehal.h"
#include "../scopehal/ChannelRenderer.h"
#include "../scopehal/TextRenderer.h"
#include "CANRenderer.h"
#include "CANDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

CANDecoder::CANDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_SERIAL)
{
	//Set up channels
	m_signalNames.push_back("Diff");
	m_channels.push_back(NULL);

	m_tq = "Time Quantum [ns]";
	m_parameters[m_tq] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_INT);
	m_parameters[m_tq].SetIntVal(156);

	m_bs1 = "Bit Segment 1 [tq]";
	m_parameters[m_bs1] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_INT);
	m_parameters[m_bs1].SetIntVal(7);

	m_bs2 = "Bit Segment 2 [tq]";
	m_parameters[m_bs2] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_INT);
	m_parameters[m_bs2].SetIntVal(5);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool CANDecoder::NeedsConfig()
{
	return true;
}

ChannelRenderer* CANDecoder::CreateRenderer()
{
	return new CANRenderer(this);
}

bool CANDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i == 0) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) && (channel->GetWidth() == 1) )
		return true;
	return false;
}

string CANDecoder::GetProtocolName()
{
	return "CAN";
}

void CANDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "CAN(%s)", m_channels[0]->m_displayname.c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void CANDecoder::Refresh()
{
	//Get the input data
	if( (m_channels[0] == NULL) )
	{
		SetData(NULL);
		return;
	}

	DigitalCapture* diff = dynamic_cast<DigitalCapture*>(m_channels[0]->GetData());
	if( (diff == NULL) )
	{
		SetData(NULL);
		return;
	}

	//Create the capture
	CANCapture* cap = new CANCapture;
	cap->m_timescale = diff->m_timescale;
	cap->m_startTimestamp = diff->m_startTimestamp;
	cap->m_startPicoseconds = diff->m_startPicoseconds;

	//Loop over the data and look for transactions
	//For now, assume equal sample rate
	bool last_diff = true;
	bool cur_diff = true;
	size_t symbol_start = 0;
	size_t bit_start = 0;
	CANSymbol::stype current_symbol = CANSymbol::TYPE_IDLE;
	uint32_t current_data = 0;
	uint8_t bitcount = 0;
	uint8_t dlc = 0;
	uint8_t stuff = 0;

	// Sync_seg + bs1 + bs2
	// FIXME
	m_nbt = m_parameters[m_tq].GetIntVal() * ( 1 + m_parameters[m_bs1].GetIntVal() + m_parameters[m_bs2].GetIntVal() );

	for(size_t i = 0; i < diff->m_samples.size(); i++)
	{
		if (symbol_start != 0 && current_symbol == CANSymbol::TYPE_SOF &&
		    ((diff->m_samples[i].m_offset - diff->m_samples[bit_start].m_offset + 1) * diff->m_timescale)
		    <
		    ((2 * m_parameters[m_bs1].GetIntVal() + 1) * m_parameters[m_tq].GetIntVal() * 500))
		{
			// We wait for the next bit
			continue;
		} else if (symbol_start != 0 && current_symbol != CANSymbol::TYPE_SOF &&
		    ((diff->m_samples[i].m_offset - diff->m_samples[bit_start].m_offset + 1) * diff->m_timescale) < (m_nbt * 1e3))
		{
			// We wait for the next bit
			continue;
		}

		cur_diff = diff->m_samples[i].m_sample;
		bit_start = i;

		if (current_symbol != CANSymbol::TYPE_IDLE)
		{
			if (stuff == 5)
			{
				// Ignore bit
				stuff = 1;
				last_diff = cur_diff;
				continue;
			} else if (cur_diff == last_diff || stuff == 0) {
				stuff++;
			} else {
				stuff = 1;
			}
		}

		// First bit
		if (current_symbol == CANSymbol::TYPE_IDLE && cur_diff && !last_diff)
		{
			symbol_start = i;
			current_symbol = CANSymbol::TYPE_SOF;
			stuff = 1;
		}
		else if (current_symbol == CANSymbol::TYPE_SOF)
		{
			cap->m_samples.push_back(CANSample(
					diff->m_samples[symbol_start].m_offset,
					diff->m_samples[i].m_offset - symbol_start,
					CANSymbol(CANSymbol::TYPE_SOF, NULL, 0)
				)
			);

			current_symbol = CANSymbol::TYPE_SID;
			symbol_start = i;
		}
		else if (current_symbol == CANSymbol::TYPE_SID)
		{
			current_data <<= 1;
			current_data |= (cur_diff)?0:1;
			bitcount++;

			if (bitcount == 11) // SID
			{
				cap->m_samples.push_back(CANSample(
						diff->m_samples[symbol_start].m_offset,
						diff->m_samples[i].m_offset - symbol_start,
						CANSymbol(CANSymbol::TYPE_SID, (uint8_t*)&current_data, 2)
					)
				);

				bitcount = 0;
				current_data = 0;
				symbol_start = i;
				current_symbol = CANSymbol::TYPE_RTR;
			}
		}
		else if (current_symbol == CANSymbol::TYPE_RTR)
		{
			current_data |= (cur_diff)?0:1;

			cap->m_samples.push_back(CANSample(
					diff->m_samples[symbol_start].m_offset,
					diff->m_samples[i].m_offset - symbol_start,
					CANSymbol(CANSymbol::TYPE_RTR, (uint8_t *)&current_data, 1)
				)
			);

			current_data = 0;
			symbol_start = i;
			current_symbol = CANSymbol::TYPE_IDE;
		}
		else if (current_symbol == CANSymbol::TYPE_IDE)
		{
			current_data |= (cur_diff)?0:1;

			cap->m_samples.push_back(CANSample(
					diff->m_samples[symbol_start].m_offset,
					diff->m_samples[i].m_offset - symbol_start,
					CANSymbol(CANSymbol::TYPE_IDE, (uint8_t *)&current_data, 1)
				)
			);

			current_data = 0;
			symbol_start = i;
			current_symbol = CANSymbol::TYPE_R0;
		}
		else if (current_symbol == CANSymbol::TYPE_R0)
		{
			current_data |= (cur_diff)?0:1;

			cap->m_samples.push_back(CANSample(
					diff->m_samples[symbol_start].m_offset,
					diff->m_samples[i].m_offset - symbol_start,
					CANSymbol(CANSymbol::TYPE_R0, (uint8_t *)&current_data, 1)
				)
			);

			current_data = 0;
			symbol_start = i;
			current_symbol = CANSymbol::TYPE_DLC;
		}
		else if (current_symbol == CANSymbol::TYPE_DLC)
		{
			bitcount++;
			current_data <<= 1;
			current_data |= (cur_diff)?0:1;

			if (bitcount == 4)
			{
				cap->m_samples.push_back(CANSample(
						diff->m_samples[symbol_start].m_offset,
						diff->m_samples[i].m_offset - symbol_start,
						CANSymbol(CANSymbol::TYPE_DLC, (uint8_t*)&current_data, 1)
					)
				);

				dlc = current_data;
				bitcount = 0;
				current_data = 0;
				symbol_start = i;
				current_symbol = CANSymbol::TYPE_DATA;
			}
		}
		else if (current_symbol == CANSymbol::TYPE_DATA)
		{
			bitcount++;
			current_data <<= 1;
			current_data |= (cur_diff)?0:1;

			if (bitcount == 8)
			{
				cap->m_samples.push_back(CANSample(
						diff->m_samples[symbol_start].m_offset,
						diff->m_samples[i].m_offset - symbol_start,
						CANSymbol(CANSymbol::TYPE_DATA, (uint8_t*)&current_data, 1)
					)
				);

				bitcount = 0;
				current_data = 0;
				symbol_start = i;
				--dlc;
				if (0 == dlc)
				{
					current_symbol = CANSymbol::TYPE_CRC;
				}
				else
				{
					current_symbol = CANSymbol::TYPE_DATA;
				}
			}
		}
		else if (current_symbol == CANSymbol::TYPE_CRC)
		{
			bitcount++;
			current_data <<= 1;
			current_data |= (cur_diff)?0:1;

			if (bitcount == 15)
			{
				cap->m_samples.push_back(CANSample(
						diff->m_samples[symbol_start].m_offset,
						diff->m_samples[i].m_offset - symbol_start,
						CANSymbol(CANSymbol::TYPE_CRC, (uint8_t*)&current_data, 2)
					)
				);

				bitcount = 0;
				current_data = 0;
				symbol_start = 0;
				current_symbol = CANSymbol::TYPE_IDLE;
			}
		}

		//Save old state
		last_diff = cur_diff;
	}

	SetData(cap);
}
