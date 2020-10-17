/**
	@file
	@author Andrés MANELLI
	@brief Implementation of CANDecoder
 */

#include "../scopehal/scopehal.h"
#include "CANDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

CANDecoder::CANDecoder(const string& color)
	: Filter(OscilloscopeChannel::CHANNEL_TYPE_COMPLEX, color, CAT_BUS)
{
	//Set up channels
	CreateInput("din");

	m_tq = "Time Quantum";
	m_parameters[m_tq] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_PS));
	m_parameters[m_tq].SetIntVal(156000);

	m_bs1 = "Bit Segment 1 [tq]";
	m_parameters[m_bs1] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_bs1].SetIntVal(7);

	m_bs2 = "Bit Segment 2 [tq]";
	m_parameters[m_bs2] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_bs2].SetIntVal(5);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool CANDecoder::NeedsConfig()
{
	return true;
}

bool CANDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i == 0) && (stream.m_channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) )
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
	snprintf(hwname, sizeof(hwname), "CAN(%s)", GetInputDisplayName(0).c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void CANDecoder::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Get the input data
	auto diff = dynamic_cast<DigitalWaveform*>(GetInputWaveform(0));

	//Create the capture
	auto cap = new CANWaveform;
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
	unsigned int nbt = m_parameters[m_tq].GetIntVal() *
		( 1 + m_parameters[m_bs1].GetIntVal() + m_parameters[m_bs2].GetIntVal() );

	size_t len = diff->m_samples.size();
	int delta1 = ((2 * m_parameters[m_bs1].GetIntVal() + 1) * m_parameters[m_tq].GetIntVal() / 2);
	for(size_t i = 0; i < len; i++)
	{
		if (symbol_start != 0 && current_symbol == CANSymbol::TYPE_SOF &&
		    ((diff->m_offsets[i] - diff->m_offsets[bit_start] + 1) * diff->m_timescale) < delta1)
		{
			// We wait for the next bit
			continue;
		}

		else if (symbol_start != 0 && current_symbol != CANSymbol::TYPE_SOF &&
		    ((diff->m_offsets[i] - diff->m_offsets[bit_start] + 1) * diff->m_timescale) < nbt)
		{
			// We wait for the next bit
			continue;
		}

		cur_diff = diff->m_samples[i];
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
			cap->m_offsets.push_back(diff->m_offsets[symbol_start]);
			cap->m_durations.push_back(diff->m_offsets[i] - symbol_start);
			cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_SOF, NULL, 0));

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
				cap->m_offsets.push_back(diff->m_offsets[symbol_start]);
				cap->m_durations.push_back(diff->m_offsets[i] - symbol_start);
				cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_SID, (uint8_t*)&current_data, 2));

				bitcount = 0;
				current_data = 0;
				symbol_start = i;
				current_symbol = CANSymbol::TYPE_RTR;
			}
		}
		else if (current_symbol == CANSymbol::TYPE_RTR)
		{
			current_data |= (cur_diff)?0:1;

			cap->m_offsets.push_back(diff->m_offsets[symbol_start]);
			cap->m_durations.push_back(diff->m_offsets[i] - symbol_start);
			cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_RTR, (uint8_t *)&current_data, 1));

			current_data = 0;
			symbol_start = i;
			current_symbol = CANSymbol::TYPE_IDE;
		}
		else if (current_symbol == CANSymbol::TYPE_IDE)
		{
			current_data |= (cur_diff)?0:1;

			cap->m_offsets.push_back(diff->m_offsets[symbol_start]);
			cap->m_durations.push_back(diff->m_offsets[i] - symbol_start);
			cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_IDE, (uint8_t *)&current_data, 1));

			current_data = 0;
			symbol_start = i;
			current_symbol = CANSymbol::TYPE_R0;
		}
		else if (current_symbol == CANSymbol::TYPE_R0)
		{
			current_data |= (cur_diff)?0:1;

			cap->m_offsets.push_back(diff->m_offsets[symbol_start]);
			cap->m_durations.push_back(diff->m_offsets[i] - symbol_start);
			cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_R0, (uint8_t *)&current_data, 1));

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
				cap->m_offsets.push_back(diff->m_offsets[symbol_start]);
				cap->m_durations.push_back(diff->m_offsets[i] - symbol_start);
				cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_DLC, (uint8_t*)&current_data, 1));

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
				cap->m_offsets.push_back(diff->m_offsets[symbol_start]);
				cap->m_durations.push_back(diff->m_offsets[i] - symbol_start);
				cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_DATA, (uint8_t*)&current_data, 1));

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
				cap->m_offsets.push_back(diff->m_offsets[symbol_start]);
				cap->m_durations.push_back(diff->m_offsets[i] - symbol_start);
				cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_CRC, (uint8_t*)&current_data, 2));

				bitcount = 0;
				current_data = 0;
				symbol_start = 0;
				current_symbol = CANSymbol::TYPE_IDLE;
			}
		}

		//Save old state
		last_diff = cur_diff;
	}

	SetData(cap, 0);
}

Gdk::Color CANDecoder::GetColor(int i)
{
	auto capture = dynamic_cast<CANWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const CANSymbol& s = capture->m_samples[i];

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

string CANDecoder::GetText(int i)
{
	auto capture = dynamic_cast<CANWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const CANSymbol& s = capture->m_samples[i];

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

