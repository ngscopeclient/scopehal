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
	, m_baudrateName("Bit Rate")
{
	//Set up channels
	CreateInput("CANH");

	m_parameters[m_baudrateName] = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_BITRATE));
	m_parameters[m_baudrateName].SetIntVal(250000);
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

	//Calculate some time scale values
	//Sample point is 3/4 of the way through the UI
	auto bitrate = m_parameters[m_baudrateName].GetIntVal();
	int64_t ps_per_ui = 1e12 / bitrate;
	int64_t samples_per_ui = ps_per_ui / diff->m_timescale;

	enum
	{
		STATE_IDLE,
		STATE_SOF,
		STATE_ID,
		STATE_EXT_ID,
		STATE_RTR,
		STATE_IDE,
		STATE_FD,
		STATE_R0,
		STATE_DLC,
		STATE_DATA,
		STATE_CRC,

		STATE_CRC_DELIM,
		STATE_ACK,
		STATE_ACK_DELIM,
		STATE_EOF
	} state = STATE_IDLE;

	//LogDebug("Starting CAN decode\n");
	//LogIndenter li;

	size_t len = diff->m_samples.size();
	int64_t tbitstart = 0;
	int64_t tblockstart = 0;
	bool vlast = true;
	int nbit = 0;
	bool sampled = false;
	bool sampled_value = false;
	bool last_sampled_value = false;
	int bits_since_toggle = 0;
	uint32_t current_field = 0;
	bool frame_is_rtr = false;
	bool extended_id = false;
	int frame_bytes_left = 0;
	int32_t frame_id = 0;
	for(size_t i = 0; i < len; i++)
	{
		bool v = diff->m_samples[i];
		bool toggle = (v != vlast);
		vlast = v;

		auto off = diff->m_offsets[i];
		auto end = diff->m_durations[i] + off;

		auto current_bitlen = off - tbitstart;

		//If we're idle, begin the SOF as soon as we hit a dominant state
		if(state == STATE_IDLE)
		{
			if(v)
			{
				tblockstart = off;
				tbitstart = off;
				nbit = 0;
				bits_since_toggle = 0;
				state = STATE_SOF;
			}
			continue;
		}

		//Ignore all transitions for the first half of the unit interval
		//TODO: resync if we get one in the very early period
		if(current_bitlen < samples_per_ui/2)
			continue;

		//When we hit 3/4 of a UI, sample the bit value.
		//Invert the sampled value since CAN uses negative logic
		if( (current_bitlen >= 3 * samples_per_ui / 4) && !sampled )
		{
			last_sampled_value = sampled_value;
			sampled = true;
			sampled_value = !v;
		}

		//Lock in a bit when either the UI ends, or we see a transition
		if(toggle || (current_bitlen >= samples_per_ui) )
		{
			/*
			LogDebug("Bit ended at %s (bits_since_toggle = %d, sampled_value = %d, last_sampled_value = %d)\n",
				Unit(Unit::UNIT_PS).PrettyPrint(off * diff->m_timescale).c_str(), bits_since_toggle,
				sampled_value, last_sampled_value);
			*/

			if(sampled_value == last_sampled_value)
				bits_since_toggle ++;

			//Don't look for stuff bits at the end of the frame
			else if(state >= STATE_ACK)
			{}

			//Discard stuff bits
			else
			{
				if(bits_since_toggle == 5)
				{
					//LogDebug("Discarding stuff bit at %s (bits_since_toggle = %d)\n",
					//	Unit(Unit::UNIT_PS).PrettyPrint(off * diff->m_timescale).c_str(), bits_since_toggle);

					tbitstart = off;
					sampled = false;
					bits_since_toggle = 1;
					continue;
				}
				else
					bits_since_toggle = 1;
			}

			//TODO: Detect and report an error if we see six consecutive bits with the same polarity

			//Read data bits
			current_field <<= 1;
			if(sampled_value)
				current_field |= 1;
			nbit ++;

			switch(state)
			{
				case STATE_IDLE:
					break;

				//SOF bit is over
				case STATE_SOF:
					cap->m_offsets.push_back(tblockstart);
					cap->m_durations.push_back(off - tblockstart);
					cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_SOF, 0));

					extended_id = false;

					tblockstart = off;
					nbit = 0;
					current_field = 0;
					state = STATE_ID;
					break;

				//Read the ID (MSB first)
				case STATE_ID:

					//When we've read 11 bits, the ID is over
					if(nbit == 11)
					{
						cap->m_offsets.push_back(tblockstart);
						cap->m_durations.push_back(end - tblockstart);
						cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_ID, current_field));

						state = STATE_RTR;

						frame_id = current_field;
					}

					break;

				//Remote transmission request
				case STATE_RTR:
					frame_is_rtr = sampled_value;

					cap->m_offsets.push_back(tbitstart);
					cap->m_durations.push_back(end - tbitstart);
					cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_RTR, frame_is_rtr));

					if(extended_id)
						state = STATE_FD;
					else
						state = STATE_IDE;

					break;

				//Identifier extension
				case STATE_IDE:
					extended_id = sampled_value;

					if(extended_id)
					{
						//The last symbol was a SRR, not a RTR
						cap->m_samples[cap->m_samples.size()-1].m_stype = CANSymbol::TYPE_SRR;

						tblockstart = off;
						nbit = 0;
						current_field = 0;
						state = STATE_EXT_ID;
					}

					else
						state = STATE_R0;

					cap->m_offsets.push_back(tbitstart);
					cap->m_durations.push_back(end - tbitstart);
					cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_IDE, extended_id));

					break;

				//Full ID
				case STATE_EXT_ID:

					//Read the other 18 bits of the ID
					if(nbit == 18)
					{
						cap->m_offsets.push_back(tblockstart);
						cap->m_durations.push_back(end - tblockstart);
						cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_ID, (frame_id << 18) | current_field));

						state = STATE_RTR;
					}

					break;

				//Reserved bit (should always be zero)
				case STATE_R0:
					cap->m_offsets.push_back(tbitstart);
					cap->m_durations.push_back(end - tbitstart);
					cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_R0, sampled_value));

					state = STATE_DLC;
					tblockstart = off;
					nbit = 0;
					current_field = 0;
					break;

				//FD mode (currently ignored)
				case STATE_FD:
					cap->m_offsets.push_back(tbitstart);
					cap->m_durations.push_back(end - tbitstart);
					cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_FD, sampled_value));

					state = STATE_R0;
					break;

				//Data length code (4 bits)
				case STATE_DLC:

					//When we've read 4 bits, the DLC is over
					if(nbit == 4)
					{
						cap->m_offsets.push_back(tblockstart);
						cap->m_durations.push_back(end - tblockstart);
						cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_DLC, current_field));

						frame_bytes_left = current_field;

						//Skip data if DLC=0
						if(frame_bytes_left == 0)
							state = STATE_CRC;
						else
							state = STATE_DATA;

						tblockstart = end;
						nbit = 0;
						current_field = 0;
					}

					break;

				//Read frame data
				case STATE_DATA:

					//Data is in 8-bit bytes
					if(nbit == 8)
					{
						cap->m_offsets.push_back(tblockstart);
						cap->m_durations.push_back(end - tblockstart);
						cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_DATA, current_field));

						//Go to CRC after we've read all the data
						frame_bytes_left --;
						if(frame_bytes_left == 0)
							state = STATE_CRC;

						//Reset for the next byte
						tblockstart = end;
						nbit = 0;
						current_field = 0;
					}

					break;

				//Read CRC value
				case STATE_CRC:

					//CRC is 15 bits long
					if(nbit == 15)
					{
						//TODO: actually check the CRC
						cap->m_offsets.push_back(tblockstart);
						cap->m_durations.push_back(end - tblockstart);
						cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_CRC_OK, current_field));

						state = STATE_CRC_DELIM;
					}

					break;

				//CRC delimiter
				case STATE_CRC_DELIM:
					cap->m_offsets.push_back(tbitstart);
					cap->m_durations.push_back(end - tbitstart);
					cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_CRC_DELIM, sampled_value));

					state = STATE_ACK;
					break;

				//ACK bit
				case STATE_ACK:
					cap->m_offsets.push_back(tbitstart);
					cap->m_durations.push_back(end - tbitstart);
					cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_ACK, sampled_value));

					state = STATE_ACK_DELIM;
					break;

				//ACK delimiter
				case STATE_ACK_DELIM:
					cap->m_offsets.push_back(tbitstart);
					cap->m_durations.push_back(end - tbitstart);
					cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_ACK_DELIM, sampled_value));

					state = STATE_EOF;
					tblockstart = end;
					nbit = 0;
					current_field = 0;
					break;

				//Read EOF
				case STATE_EOF:

					//EOF is 7 bits long
					if(nbit == 7)
					{
						cap->m_offsets.push_back(tblockstart);
						cap->m_durations.push_back(end - tblockstart);
						cap->m_samples.push_back(CANSymbol(CANSymbol::TYPE_EOF, current_field));

						state = STATE_IDLE;
					}

					break;

				default:
					break;
			}

			//Start the next bit
			tbitstart = off;
			sampled = false;
		}
	}

	SetData(cap, 0);
}

Gdk::Color CANDecoder::GetColor(int i)
{
	auto capture = dynamic_cast<CANWaveform*>(GetData(0));
	if(capture != NULL)
	{
		const CANSymbol& s = capture->m_samples[i];

		switch(s.m_stype)
		{
			case CANSymbol::TYPE_SOF:
				return m_standardColors[COLOR_PREAMBLE];

			case CANSymbol::TYPE_R0:
				if(!s.m_data)
					return m_standardColors[COLOR_PREAMBLE];
				else
					return m_standardColors[COLOR_ERROR];

			case CANSymbol::TYPE_ID:
			case CANSymbol::TYPE_IDE:
				return m_standardColors[COLOR_ADDRESS];

			case CANSymbol::TYPE_RTR:
			case CANSymbol::TYPE_FD:
				return m_standardColors[COLOR_CONTROL];

			case CANSymbol::TYPE_DLC:
				if(s.m_data > 8)
					return m_standardColors[COLOR_ERROR];
				else
					return m_standardColors[COLOR_CONTROL];

			case CANSymbol::TYPE_DATA:
				return m_standardColors[COLOR_DATA];

			case CANSymbol::TYPE_CRC_OK:
				return m_standardColors[COLOR_CHECKSUM_OK];

			case CANSymbol::TYPE_CRC_DELIM:
			case CANSymbol::TYPE_ACK_DELIM:
			case CANSymbol::TYPE_EOF:
			case CANSymbol::TYPE_SRR:
				if(s.m_data)
					return m_standardColors[COLOR_PREAMBLE];
				else
					return m_standardColors[COLOR_ERROR];

			case CANSymbol::TYPE_ACK:
				if(!s.m_data)
					return m_standardColors[COLOR_CHECKSUM_OK];
				else
					return m_standardColors[COLOR_CHECKSUM_BAD];

			case CANSymbol::TYPE_CRC_BAD:
			default:
				return m_standardColors[COLOR_ERROR];
		}
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
				return "SOF";

			case CANSymbol::TYPE_ID:
				snprintf(tmp, sizeof(tmp), "ID %03x", s.m_data);
				break;

			case CANSymbol::TYPE_FD:
				if(s.m_data)
					return "FD";
				else
					return "STD";

			case CANSymbol::TYPE_RTR:
				if(s.m_data)
					return "REQ";
				else
					return "DATA";

			case CANSymbol::TYPE_IDE:
				if(s.m_data)
					return "EXT";
				else
					return "BASE";

			case CANSymbol::TYPE_SRR:
				return "SRR";

			case CANSymbol::TYPE_R0:
				return "RSVD";

			case CANSymbol::TYPE_DLC:
				snprintf(tmp, sizeof(tmp), "Len %u", s.m_data);
				break;

			case CANSymbol::TYPE_DATA:
				snprintf(tmp, sizeof(tmp), "%02x", s.m_data);
				break;

			case CANSymbol::TYPE_CRC_OK:
			case CANSymbol::TYPE_CRC_BAD:
				snprintf(tmp, sizeof(tmp), "CRC: %04x", s.m_data);
				break;

			case CANSymbol::TYPE_CRC_DELIM:
				return "CRC DELIM";

			case CANSymbol::TYPE_ACK:
				if(!s.m_data)
					return "ACK";
				else
					return "NAK";

			case CANSymbol::TYPE_ACK_DELIM:
				return "ACK DELIM";

			case CANSymbol::TYPE_EOF:
				return "EOF";

			default:
				return "ERROR";
		}
		return string(tmp);
	}

	return "ERROR";
}

