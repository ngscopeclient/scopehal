/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
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
	@author Marcin Dawidowicz
	@brief Implementation of Ethernet Clause 73 Autonegotiation Decoder
 */

#include "../scopehal/scopehal.h"
#include "EthernetClause73AutonegotiationDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

EthernetClause73AutonegotiationDecoder::EthernetClause73AutonegotiationDecoder(const string& color)
	: Filter(color, CAT_SERIAL)
	, m_displayformat("Display Format")
{
	AddProtocolStream("data");
	CreateInput("data");
	CreateInput("clk");

	m_parameters[m_displayformat] = MakeDisplayFormatParameter();
}

FilterParameter EthernetClause73AutonegotiationDecoder::MakeDisplayFormatParameter()
{
	auto f = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	f.AddEnumValue("Compact", FORMAT_COMPACT);
	f.AddEnumValue("Detailed", FORMAT_DETAILED);
	f.SetIntVal(FORMAT_DETAILED);

	return f;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool EthernetClause73AutonegotiationDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 2) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	return false;
}

string EthernetClause73AutonegotiationDecoder::GetProtocolName()
{
	return "Ethernet Clause 73 Autonegotiation";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

static size_t CountConsecutiveBits(SparseDigitalWaveform &data, size_t start_idx)
{
	if(start_idx >= data.m_samples.size()) {
		return 0;
	}

	auto bit_val = data.m_samples[start_idx];
	size_t count = 1;

	for(size_t i = start_idx + 1; i < data.m_samples.size(); i++) {
		if(data.m_samples[i] == bit_val)
			count++;
		else
			break;
	}

	return count;
}

static vector<char> DecodeAutonegPage(SparseDigitalWaveform &data, size_t start_idx, size_t& end_idx)
{
	vector<char> decoded;
	size_t i = start_idx;

	while(i + 1 < data.m_samples.size()) {
		int bit1 = data.m_samples[i];
		int bit2 = data.m_samples[i + 1];

		if(bit1 == bit2) {
			// Same bits - could be 0 or termination
			size_t count = CountConsecutiveBits(data, i);

			if(count > 2) {
				// Termination
				end_idx = i;
				return decoded;
			} else {
				// Exactly 2 same bits = 0
				decoded.push_back(0);
				i += 2;
			}
		} else {
			// Different bits (01 or 10) = 1
			decoded.push_back(1);
			i += 2;
		}
	}

	end_idx = i;
	return decoded;
}

static bool ParseCodePage(const vector<char>& bits, Clause73CodePage& page)
{
	if(bits.size() != 49)
		return false;

	// Extract fields (D[0] is LSB - first element in vector)
	// D[4:0] Selector Field
	page.selector_field = 0;
	for(int i = 0; i < 5; i++)
		if(bits[i])
			page.selector_field |= (1UL << i);

	// D[9:5] Echoed Nonce
	page.echoed_nonce = 0;
	for(int i = 5; i < 10; i++)
		if(bits[i])
			page.echoed_nonce |= (1UL << (i - 5));

	page.c2_reserved = bits[10];
	page.c1_pause = bits[11];
	page.c0_pause = bits[12];

	// D[15:13] RF/Ack/NP
	page.rf = bits[13];
	page.ack = bits[14];
	page.np = bits[15];

	// D[20:16] Transmitted Nonce
	page.transmitted_nonce = 0;
	for(int i = 16; i < 21; i++)
		if(bits[i])
			page.transmitted_nonce |= (1UL << (i - 16));

	// D[43:21] Technology Ability
	page.technology_ability = 0;
	for(int i = 21; i < 44; i++)
		if(bits[i])
			page.technology_ability |= (1UL << (i - 21));

	// D[47:44] FEC capability
	page.fec = 0;
	for(int i = 44; i < 48; i++)
		if(bits[i])
			page.fec |= (1UL << (i - 44));

	// D[48] Code
	page.code = bits[48];

	return true;
}

struct StartSequence
{
	size_t start_idx;       // Position after the 8-bit preamble
};

static vector<StartSequence> FindAllAutonegStarts(SparseDigitalWaveform &data)
{
	vector<StartSequence> starts;

	if (data.m_samples.size() < 8) {
		return starts;
	}

	uint8_t val = 0;
	for(auto bit = 0; bit < 7; bit++) {
		val <<= 1;
		val |= data.m_samples[bit] ? 1 : 0;
	}

	for(size_t i = 7; i <= data.m_samples.size(); ++i) {
		val <<= 1;
		val |= data.m_samples[i] ? 1 : 0;

		// Check for 00001111 or 11110000
		if ( (val == 0xF0) || (val == 0x0F) ) {
			starts.push_back({i + 1});
		}
	}

	return starts;
}

template <std::size_t X>
static std::string FormatBitsX(std::uint64_t val)
{
    static_assert(X > 0, "X must be > 0");
    static_assert(X <= 64, "X must be <= 64 for uint64_t");

    std::array<char, X + 1> buf{}; // +1 for null terminator

    for (std::size_t i = 0; i < X; ++i) {
        buf[X - 1 - i] = (val & (1UL << i)) ? '1' : '0';
    }
    buf[X] = '\0';

    return std::string(buf.data());
}

void EthernetClause73AutonegotiationDecoder::Refresh()
{
	LogTrace("EthernetClause73AutonegotiationDecoder::Refresh\n");
	LogIndenter li;

	if(!VerifyAllInputsOK()) {
		SetData(nullptr, 0);
		return;
	}

	// Get the input data
	auto din = GetInputWaveform(0);
	auto clkin = GetInputWaveform(1);
	din->PrepareForCpuAccess();
	clkin->PrepareForCpuAccess();

	// Create the capture
	auto cap = new Clause73Waveform(m_parameters[m_displayformat]);
	cap->m_timescale = 1;
	cap->m_startTimestamp = din->m_startTimestamp;
	cap->m_startFemtoseconds = din->m_startFemtoseconds;
	cap->PrepareForCpuAccess();

	// Record the value of the data stream at each clock edge
	SparseDigitalWaveform data;
	SampleOnAnyEdgesBase(din, clkin, data);
	data.PrepareForCpuAccess();

	// Check if we have enough data
	if(data.m_samples.size() < 8) {
		SetData(nullptr, 0);
		return;
	}

	// Find all autonegotiation starts
	vector<StartSequence> start_sequences = FindAllAutonegStarts(data);

	if(start_sequences.empty()) {
		SetData(nullptr, 0);
		return;
	}

	LogTrace("Found %zu autonegotiation sequences\n", start_sequences.size());

	// Process each sequence
	for(size_t seq_idx = 0; seq_idx < start_sequences.size(); seq_idx++) {
		size_t start_idx = start_sequences[seq_idx].start_idx;

		// Decode the page
		size_t end_idx;
		vector<char> decoded_bits = DecodeAutonegPage(data, start_idx, end_idx);

		// Only process if we have exactly 49 bits (valid Clause 73 page)
		if(decoded_bits.size() == 49) {
			Clause73CodePage page;
			if(ParseCodePage(decoded_bits, page)) {

				// Map the bit position to a timestamp
				size_t preamble_start = start_idx - 8;
				if(preamble_start < data.m_samples.size()) {
					int64_t offset = data.m_offsets[preamble_start];
					int64_t duration = 1; // Default duration

					// Try to get duration from the end index if available
					if(end_idx < data.m_offsets.size() && end_idx > preamble_start) {
						duration = data.m_offsets[end_idx] - data.m_offsets[preamble_start];
					} else if(start_idx + 1 < data.m_durations.size()) {
						duration = data.m_durations[start_idx] * 49; // Approximate
					}

					cap->m_offsets.push_back(offset);
					cap->m_durations.push_back(duration);
					cap->m_samples.push_back(page);
				}
			}
		}
	}

	SetData(cap, 0);
	cap->MarkModifiedFromCpu();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Clause73Waveform

string Clause73Waveform::GetColor(size_t i)
{
	(void) i;
	return StandardColors::colors[StandardColors::COLOR_DATA];
}

string Clause73Waveform::GetText(size_t i)
{
	const Clause73CodePage& page = m_samples[i];

	EthernetClause73AutonegotiationDecoder::DisplayFormat format =
		(EthernetClause73AutonegotiationDecoder::DisplayFormat)m_displayformat.GetIntVal();

	if(format == EthernetClause73AutonegotiationDecoder::FORMAT_COMPACT) {
		// Compact format - single line
		char tmp[512];

		snprintf(tmp, sizeof(tmp),
			"Selector = 0x%02x | Tech Ability = 0x%06x | Nonce Echoed = 0x%02x | Nonce Tx = 0x%02x | Ack=%c | NP=%c | RF=%c | C=%d|%d%d",
			page.selector_field,
			page.technology_ability,
			page.echoed_nonce,
			page.transmitted_nonce,
			page.ack ? '1' : '0',
			page.np ? '1' : '0',
			page.rf ? '1' : '0',
			page.c2_reserved,
			page.c1_pause,
			page.c0_pause
		);

		return string(tmp);
	} else {
		// Detailed format - show all fields with bit positions
		// string out;
		char out[512];
		int pos = 0;

		// D[4:0] Selector Field
		pos += snprintf(out + pos, sizeof(out) - pos,
			"D[4:0] Selector: %s(0x%02x) | ",
			FormatBitsX<5>(page.selector_field).c_str(),
			page.selector_field);

		// D[9:5] Echoed Nonce
		pos += snprintf(out + pos, sizeof(out) - pos,
			"D[9:5] Echoed Nonce: %s(0x%02x) | ",
			FormatBitsX<5>(page.echoed_nonce).c_str(),
			page.echoed_nonce);

		pos += snprintf(out + pos, sizeof(out) - pos,
			"C[2] Reserved: %d | ",
			page.c2_reserved);

		pos += snprintf(out + pos, sizeof(out) - pos,
			"C[1:0] Pause: %d%d(0x%x) | ",
			page.c1_pause,
			page.c0_pause,
			(page.c1_pause + 2*page.c0_pause));

		// D[15:13] RF/Ack/NP
		pos += snprintf(out + pos, sizeof(out) - pos,
			"D[15:13] RF/Ack/NP: %d%d%d | ",
			page.rf,
			page.ack,
			page.np);

		// D[20:16] Transmitted Nonce
		pos += snprintf(out + pos, sizeof(out) - pos,
			"D[20:16] Tx Nonce: %s(0x%02x) | ",
			FormatBitsX<5>(page.transmitted_nonce).c_str(),
			page.transmitted_nonce);

		// D[43:21] Technology Ability
		pos += snprintf(out + pos, sizeof(out) - pos,
			"D[43:21] Tech Ability: %s | ",
			FormatBitsX<23>(page.technology_ability).c_str());

		// D[47:44] FEC
		pos += snprintf(out + pos, sizeof(out) - pos,
			"D[47:44] FEC: %s(0x%01x) | ",
			FormatBitsX<4>(page.fec).c_str(),
			page.fec);

		// D[48] Code
		pos += snprintf(out + pos, sizeof(out) - pos,
			" D[48] Code: %d\n",
			page.code);

		return out;
	}
}
