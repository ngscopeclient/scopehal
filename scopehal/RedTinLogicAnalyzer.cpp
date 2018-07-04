/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2017 Andrew D. Zonenberg                                                                          *
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
	@author Andrew D. Zonenberg
	@brief Implementation of RedTinLogicAnalyzer
 */

#include "scopehal.h"
#include "OscilloscopeChannel.h"
#include "RedTinLogicAnalyzer.h"
#include "RedTin_opcodes_enum.h"
#include "ProtocolDecoder.h"
//#include "StateDecoder.h"

#include <memory.h>

/*
#include <NOCSysinfo_constants.h>
#include <RPCv2Router_type_constants.h>
#include <RPCv2Router_ack_constants.h>
*/

using namespace std;

int bit_test_pair(int state_0, int state_1, int current_1, int old_1, int current_0, int old_0);
int bit_test(int state, int current, int old);
int MakeTruthTable(int state_0, int state_1);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Connects to a UART and reads the stuff off it
 */
RedTinLogicAnalyzer::RedTinLogicAnalyzer(const string& tty, int baud)
{
	m_uart = new UART(tty, baud);

	m_laname = tty;
	LoadChannels();
	ResetTriggerConditions();
}

/**
	@brief Connects to nocswitch and then establishes a connection to the LA core
 */
/*
RedTinLogicAnalyzer::RedTinLogicAnalyzer(const std::string& host, unsigned short port, const std::string& nochost)
	: m_nameserver(NULL)
{
	//leave triggers empty

	//Connect to nocswitch
	m_iface.Connect(host, port);

	//and the LA
	Connect(nochost);
}
*/

/**
	@brief Connects to nocswitch but does not establish a connection to the LA core.

	Useful if doing LA device enumeration. Use Connect() to connect to a specific LA on the device. The result of
	calling any other member functions before Connect() is undefined.
 */
/*
RedTinLogicAnalyzer::RedTinLogicAnalyzer(const std::string& host, unsigned short port)
	: m_nameserver(NULL)
{
	//leave triggers empty

	//Connect to nocswitch only
	m_iface.Connect(host, port);
}
*/

/**
	@brief Connects to the LA core
 */
/*
void RedTinLogicAnalyzer::Connect(const std::string& nochost)
{
	//Connect to the LA
	m_nameserver = new NameServer(&m_iface);
	m_scopeaddr = m_nameserver->ForwardLookup(nochost);
	printf("Logic analyzer is at %04x\n", m_scopeaddr);
	m_nochost = nochost;

	//Get channel list
	LoadChannels();

	//Clear out trigger
	ResetTriggerConditions();
}
*/
RedTinLogicAnalyzer::~RedTinLogicAnalyzer()
{
	if(m_uart)
		delete m_uart;
	//delete m_nameserver;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Information queries

bool RedTinLogicAnalyzer::Ping()
{
	LogDebug("Pinging\n");

	int npings = 10;
	for(int i=0; i<npings; i++)
	{
		LogDebug("    %d/%d\n", i, npings);

		uint8_t op = REDTIN_PING;
		m_uart->Write(&op, 1);
		uint8_t rxbuf;
		if(!m_uart->Read(&rxbuf, 1))
		{
			LogError("Couldn't get ping\n");
			return false;
		}

		if(rxbuf != REDTIN_PING)
		{
			LogError("Bad ping reply (got %02x, expected %02x)\n", rxbuf, op);
		}
	}
	return true;
}

unsigned int RedTinLogicAnalyzer::GetInstrumentTypes()
{
	return INST_OSCILLOSCOPE;
}

string RedTinLogicAnalyzer::GetName()
{
	return m_laname;
}

string RedTinLogicAnalyzer::GetVendor()
{
	return "RED TIN LA core";
}

string RedTinLogicAnalyzer::GetSerial()
{
	return "NoSerialNumber";
}

void RedTinLogicAnalyzer::LoadChannels()
{
	LogDebug("Logic analyzer: loading channel metadata\n");
	LogIndenter li;

	if(m_uart)
	{
		//Read the symbol table ROM
		uint8_t op = REDTIN_READ_SYMTAB;
		m_uart->Write(&op, 1);
		uint8_t rxbuf[2048];
		if(!m_uart->Read(rxbuf, 2048))
		{
			LogError("Couldn't read symbol ROM\n");
			return;
		}

		//Flip the array around
		FlipByteArray(rxbuf, 2048);

		//Skip the leading zeroes
		uint8_t* end = rxbuf + 2048;
		uint8_t* ptr = rxbuf;
		for(; ptr < (end-11) && (*ptr == 0); ptr ++)
		{}

		//First nonzero bytes should be "DEBUGROM"
		if(0 != memcmp(ptr, "DEBUGROM", 8))
		{
			LogError("Missing magic number at start of symbol ROM\n");
			return;
		}
		ptr += 8;

		//Should have a 0-1-0 sync pattern.
		//If we see 1-0-0, Vivado synthesis is being derpy and scrambling our ROM.
		uint8_t good_sync[3] = {0, 1, 0};
		uint8_t bad_sync[3] = {1, 0, 0};
		if(0 == memcmp(ptr, good_sync, 3))
			LogDebug("Good sync\n");
		else if(0 == memcmp(ptr, bad_sync, 3))
		{
			LogWarning("Symbol table was built with buggy Vivado!\n");
			LogWarning("WORKAROUND: Use {\"foo\", 8'h0} instead of \"foo\\0\"\n");
			return;
		}
		else
		{
			LogDebug("Bad sync pattern (not correct or known Vivado bug)\n");
			return;
		}
		ptr += 3;

		//Verify we have room in the buffer, then read metadata about the capture
		if(ptr >= (end - 12) )
		{
			LogError("Not enough room for full header\n");
			return;
		}

		m_timescale = (ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | ptr[3];
		m_depth = (ptr[4] << 24) | (ptr[5] << 16) | (ptr[6] << 8) | ptr[7];
		m_width = (ptr[8] << 24) | (ptr[9] << 16) | (ptr[10] << 8) | ptr[11];
		ptr += 12;

		LogDebug("Timescale: %u ps\n", m_timescale);
		LogDebug("Buffer: %u words of %u samples\n", m_depth, m_width);

		//From here on, we have a series of packets that should end at the end of the buffer:
		//Signal name (null terminated)
		//Signal width (1 byte)
		//Reserved for protocol decodes etc (1 byte)
		while(ptr < end)
		{
			unsigned int width = 0;
			unsigned int type = 0;

			//Read signal name, then skip trailing null
			string name;
			for(; ptr+1<end && (*ptr != 0); ptr ++)
			{
				//LogDebug("    Reading %c (%02x)\n", *ptr, (int)*ptr);
				name += *ptr;
			}
			//LogDebug("    Skipping null (%02x)\n", (int)*ptr);
			ptr ++;

			//We now have the signal width, then reserved type field
			if(ptr + 2 > end)
			{
				LogError("Last signal (%s) is truncated\n", name.c_str());
				break;
			}

			width = ptr[0];
			type = ptr[1];
			ptr += 2;

			LogDebug("Signal %s has width %u, type %u\n", name.c_str(), width, type);

			//Allocate a color for it
			string color = GetDefaultChannelColor(m_channels.size());

			//Normal channel (no protocol decoders)
			if(type == 0)
			{
				OscilloscopeChannel* chan = NULL;
				if(width == 1)
				{
					m_channels.push_back(chan = new OscilloscopeChannel(
						name, OscilloscopeChannel::CHANNEL_TYPE_DIGITAL, color));
				}
				else
				{
					m_channels.push_back(chan = new OscilloscopeChannel(
						name, OscilloscopeChannel::CHANNEL_TYPE_DIGITAL, color, width));
				}
			}

			//TODO
			else
			{
				LogError("Don't have support for protocol decoders yet\n");
				continue;
			}
		}

		if(ptr != end)
		{
			LogError("Data didn't end exactly at end of buffer\n");
			return;
		}
	}

	else
	{
		//TODO: NoC transport
	}

	/*
	//Get the table
	DMAMessage msg;
	msg.from = 0x0000;
	msg.to = m_scopeaddr;
	msg.opcode = DMA_OP_READ_REQUEST;
	msg.len = 512;
	msg.address = 0x10000000;
	m_iface.SendDMAMessage(msg);

	DMAMessage rxm;
	if(!m_iface.RecvDMAMessageBlockingWithTimeout(rxm, 5))
	{
		throw JtagExceptionWrapper(
			"Message timeout",
			"",
			JtagException::EXCEPTION_TYPE_FIRMWARE);
	}

	//Color table
	int color_num = 0;
	*/

	/*
		Serialized data format
			Time scale (32 bits, big endian)
			Depth (32 bits, big endian)
			Per channel
				Signal type
					0 = regular signal
						Signal width (8 bits)
						Signal name length (8 bits)
						Signal name data
						Constant table filename length (8 bits)
						Constant table filename data
					1 = protocol decoder
						Signal name length (8 bits)
						Signal name data
						Decoder name length (8 bits)
						Decoder name data
		A frame with zero for signal width indicates the end of the buffer.
	*/
	/*
	const unsigned char* data = (const unsigned char*)&rxm.data[0];
	int pos = 0;
	for(; pos<4; pos++)
		m_timescale = (m_timescale << 8) | data[pos];
	for(; pos<8; pos++)
		m_width = (m_width << 8) | data[pos];
	for(; pos<12; pos++)
		m_depth = (m_depth << 8) | data[pos];
	printf("    System clock period is %" PRIu32 " ps\n", m_timescale);
	printf("    LA has %" PRIu32 " channels\n", m_width);
	printf("    Capture depth is %" PRIu32 " samples\n", m_depth);
	while(pos < 2048)
	{
		//Get the channel type
		int type = data[pos++];

		//Allocate a color for it
		string color = color_table[color_num];
		color_num = (color_num + 1) % (sizeof(color_table) / sizeof(color_table[0]));

		//Normal channel
		//TODO: Don't have constant filenames here, use the decoder syntax instead
		if(type == 0)
		{
			//Unpack the serialized data
			int width = data[pos++];
			if(width == 0)
				break;
			string sname = ReadString(data, pos);
			string fname = ReadString(data, pos);

			//Add the signals
			OscilloscopeChannel* chan = NULL;
			if(width == 1)
			{
				m_channels.push_back(chan = new OscilloscopeChannel(
					sname, OscilloscopeChannel::CHANNEL_TYPE_DIGITAL, color));
			}
			else
			{
				m_channels.push_back(chan = new OscilloscopeChannel(
					sname, OscilloscopeChannel::CHANNEL_TYPE_DIGITAL, color, false, width));
			}

			//If decoding, create the constant decoder
			if(fname != "")
			{
				//Create the decoder (using the same color as the original signal)
				ProtocolDecoder* decoder = new StateDecoder(sname, color, *m_nameserver, fname);
				decoder->SetInput(0, chan);
				m_channels.push_back(decoder);

				//Hide the original channel
				chan->m_visible = false;
			}
		}

		//Protocol decoder
		else if(type == 1)
		{
			//Signal name and decoder type
			string sname = ReadString(data, pos);
			string tname = ReadString(data, pos);

			//Create the protocol decoder
			ProtocolDecoder* decoder = ProtocolDecoder::CreateDecoder(
				tname,
				sname,
				color,
				*m_nameserver);

			//Number of inputs
			int ninputs = data[pos++];

			//Read the strings
			for(int j=0; j<ninputs; j++)
			{
				string input = ReadString(data, pos);
				decoder->SetInput(j, GetChannel(input));
			}

			//Add the decoder
			m_channels.push_back(decoder);
		}

		//Nope, don't know what
		else
		{
			throw JtagExceptionWrapper(
				"Signal name ROM has bad signal type",
				"",
				JtagException::EXCEPTION_TYPE_FIRMWARE);
		}
	}
	*/
	//Initialize trigger
	m_triggers.clear();
	for(uint32_t i=0; i<m_width; i++)
		m_triggers.push_back(RedTinLogicAnalyzer::TRIGGER_TYPE_DONTCARE);
}

/*
string RedTinLogicAnalyzer::ReadString(const unsigned char* data, int& pos)
{
	int namelen = data[pos++];
	char str[257] = {0};  //namelen was 8 bits so can never be >256
	memcpy(str, data+pos, namelen);
	pos += namelen;

	return string(str);
}
*/

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering

Oscilloscope::TriggerMode RedTinLogicAnalyzer::PollTrigger()
{
	if(m_uart)
	{
		uint8_t opcode;
		m_uart->Read(&opcode, 1);
		if(opcode != REDTIN_TRIGGER_NOTIF)
		{
			LogWarning("Got bad trigger opcode, ignoring\n");
			return TRIGGER_MODE_RUN;
		}

		return TRIGGER_MODE_TRIGGERED;
	}

	else
	{
		/*
		//Wait for trigger notification
		RPCMessage rxm;
		//printf("[%.3f] Trigger poll\n", GetTime());
		if(m_iface.RecvRPCMessage(rxm))
		{
			if(rxm.from != m_scopeaddr)
			{
				printf("Got message from unexpected source, ignoring\n");
				return TRIGGER_MODE_RUN;
			}
			if((rxm.type == RPC_TYPE_INTERRUPT) && (rxm.callnum == 0) )
			{
				printf("Triggered\n");
				return TRIGGER_MODE_TRIGGERED;
			}
			else
			{
				printf("Unknown opcode, ignoring\n");
				return TRIGGER_MODE_RUN;
			}
		}
		*/
		return TRIGGER_MODE_RUN;
	}
}

bool RedTinLogicAnalyzer::AcquireData(sigc::slot1<int, float> progress_callback)
{
	LogDebug("Acquiring data...\n");
	LogIndenter li;

	//Number of columns to read
	const uint32_t read_cols = m_width / 32;

	bool* rx_buf = new bool[m_depth * m_width];
	uint32_t* rx_buf_packed = new uint32_t[m_depth * read_cols];
	uint32_t* timestamp = new uint32_t[m_depth];

	//Read out the data
	if(m_uart)
	{
		//Read the data back
		for(uint32_t row=0; row<m_depth; row++)
		{
			progress_callback(static_cast<float>(row) / (m_depth));

			//Request readback (one read request per row, for simple lock-step flow control)
			uint8_t op = REDTIN_READ_DATA;
			if(row != 0)
				op = REDTIN_READ_CONTINUE;
			if(!m_uart->Write(&op, 1))
				return false;

			//LogDebug("Row %u\n", row);
			//LogIndenter li;

			//Read timestamp
			if(!m_uart->Read((unsigned char*)(timestamp + row), 4))
				return false;

			//Read data
			if(!m_uart->Read((unsigned char*)(rx_buf_packed + (row*read_cols)), 4*read_cols))
				return false;

			//Unpack the row into a bool[]
			for(uint32_t col=0; col<m_width; col++)
			{
				uint32_t wordoff = col / 32;
				uint32_t bitoff = col % 32;
				rx_buf[row*m_width + col] = (rx_buf_packed[row*read_cols + wordoff] >> bitoff) & 1;
			}

			/*
			LogDebug("Time: %u\n", timestamp[row]);
			LogDebug("        Data: ");
			for(int i=(int)m_width-1; i>=0; i--)
				LogDebug("%d", rx_buf[m_width*row + i]);
			LogDebug("\n");
			*/
		}
	}

	else
	{
		/*
		//Number of words in a single column
		//Columns are always 32 bits wide
		//const uint32_t colsize = 4 * m_depth;

		//Read the data
		//Blocks 0...read_cols-1 are data, read_cols is timestamp
		for(uint32_t col=0; col<=read_cols; col++)
		{
			progress_callback(static_cast<float>(col) / (read_cols+1));

			//Read in blocks of 512 words x 32 samples
			for(uint32_t start=0; start<m_depth; start += 512)
			{
				//Request read
				DMAMessage rmsg;
				rmsg.from = 0x0000;
				rmsg.to = m_scopeaddr;
				rmsg.opcode = DMA_OP_READ_REQUEST;
				rmsg.len = 512;
				rmsg.address = 0x00000000 + colsize*col + start*4;	//start is in words, not bytes, so need to multiply
				m_iface.SendDMAMessage(rmsg);

				//Get data back
				DMAMessage rxm;
				if(!m_iface.RecvDMAMessageBlockingWithTimeout(rxm, 5))
				{
					delete[] rx_buf;
					delete[] timestamp;

					throw JtagExceptionWrapper(
						"Message timeout",
						"",
						JtagException::EXCEPTION_TYPE_FIRMWARE);
				}

				//Sanity check origin
				if( (rxm.from != m_scopeaddr) || (rxm.address != rmsg.address) || (rxm.len != 512) )
				{
					delete[] rx_buf;
					delete[] timestamp;

					throw JtagExceptionWrapper(
						"Invalid message received",
						"",
						JtagException::EXCEPTION_TYPE_FIRMWARE);
				}

				//We don't have sample -1 so arbitrarily declare the start of sample 0 to be T=0
				if(col == read_cols)
					timestamp[0] = 0;

				//Flip byte/bit ordering as necessary
				if(col == read_cols)
					FlipEndian32Array((unsigned char*)&rxm.data[0], 2048);
				else
					FlipBitArray((unsigned char*)&rxm.data[0], 2048);

				//Crunch it
				for(int i=0; i<512; i++)
				{
					//Data
					if(col < read_cols)
					{
						for(int j=0; j<32; j++)
							rx_buf[m_width*(start+i) + col*32 + (31 - j)] = (rxm.data[i] >> j) & 1;
					}

					//Timestamp
					else
						timestamp[start+i] = rxm.data[i];
				}
			}
		}
		*/
	}

	//Pre-process the buffer
	//If two samples in a row are identical (incomplete compression, etc) combine them
	//Do not merge the first two samples. This ensures that we always have a line to draw.
	unsigned int write_ptr = 2;
	for(unsigned int read_ptr=2; read_ptr<m_depth; read_ptr++)
	{
		//Invariant: read_ptr >= write_ptr

		//Check if they're equal
		bool equal = true;
		if(read_ptr > 0)
		{
			for(uint32_t j=0; j<m_width; j++)
				equal &= (rx_buf[read_ptr*m_width + j] == rx_buf[(read_ptr-1)*m_width + j]);
		}

		//Copy the data
		for(uint32_t j=0; j<m_width; j++)
			rx_buf[write_ptr*m_width + j] = rx_buf[read_ptr*m_width + j];

		//If equal, merge them
		if(equal)
		{
			timestamp[write_ptr] += timestamp[read_ptr];
			//do not increment write pointer since we merged
		}

		//Otherwise, copy
		else
		{
			timestamp[write_ptr] = timestamp[read_ptr];
			write_ptr ++;
		}
	}

	int sample_count = write_ptr - 1;
	LogDebug("Final sample count: %d\n", sample_count);

	//Get channel info
	int nstart = m_width-1;
	for(size_t i=0; i<m_channels.size(); i++)
	{
		OscilloscopeChannel* chan = m_channels[i];

		//If the channel is procedural, skip it (no effect on the actual data)
		if(dynamic_cast<ProtocolDecoder*>(m_channels[i]) != NULL)
			continue;

		int width = chan->GetWidth();
		int hi = nstart;
		int lo = nstart - width + 1;
		nstart -= width;
		LogDebug("Channel %s is %d bits wide, from %d to %d\n", chan->m_displayname.c_str(), width, hi, lo);

		//Set channel info
		if(width == 1)
		{
			DigitalCapture* capture = new DigitalCapture;
			capture->m_timescale = m_timescale;
			int64_t last_timestamp = 0;
			for(int j=0; j<sample_count; j++)
			{
				last_timestamp += timestamp[j];

				//Duration - until start of next sample
				int64_t duration = 1;
				if(j < (sample_count - 1))
					duration = timestamp[j+1];

				capture->m_samples.push_back(DigitalSample(last_timestamp, duration, rx_buf[m_width*j + hi]));
			}
			chan->SetData(capture);
		}
		else
		{
			DigitalBusCapture* capture = new DigitalBusCapture;
			capture->m_timescale = m_timescale;
			int64_t last_timestamp = 0;
			for(int j=0; j<sample_count; j++)
			{
				last_timestamp += timestamp[j];

				//Duration - until start of next sample
				int64_t duration = 1;
				if(j < (sample_count - 1))
					duration = timestamp[j+1];

				//Add a new sample
				std::vector<bool> row;
				for(int k=hi; k>=lo; k--)
					row.push_back(rx_buf[m_width*j + k]);
				capture->m_samples.push_back(DigitalBusSample(last_timestamp, duration, row));
			}
			chan->SetData(capture);
		}
	}

	//Refresh protocol decoders
	for(size_t i=0; i<m_channels.size(); i++)
	{
		ProtocolDecoder* decoder = dynamic_cast<ProtocolDecoder*>(m_channels[i]);
		if(decoder != NULL)
			decoder->Refresh();
	}

	delete[] timestamp;
	delete[] rx_buf;
	delete[] rx_buf_packed;
	return true;
}

void RedTinLogicAnalyzer::StartSingleTrigger()
{
	//Build the full bitmask set
	//This creates the truth tables in LOGICAL order, not bitstream order.
	vector<int> truth_tables;
	for(uint32_t i=0; i<m_width; i+=2)
		truth_tables.push_back(MakeTruthTable(m_triggers[i], m_triggers[i+1]));

	//Debug
	/*
	LogDebug("Truth tables\n");
	for(uint32_t i=0; i<truth_tables.size(); i++)
		LogDebug("    %08x\n", truth_tables[i]);
	*/

	/*
	//We send the bitstream to the board as a DMA packet (WIDTH/2 32-bit words).
	//Set up the initial message parameters
	DMAMessage trigmsg;
	trigmsg.from = 0x0000;
	trigmsg.to = m_scopeaddr;
	trigmsg.opcode = DMA_OP_WRITE_REQUEST;
	trigmsg.len = 0;
	trigmsg.address = 0x20000000;
	*/

	vector<uint32_t> trigger_bitstream;

	//Generate the configuration bitstream.
	//Each bitplane configures one LUT, each word corresponds to one entry in all 32 LUTs.
	//32 words configure a full set of 32 LUTs, additional LUTs are configured with another set.
	//Note that since we shift into the LSB shift registers, we need to load the MOST significant block first.
	uint32_t nblocks = m_width / 64;
	for(int32_t block=nblocks-1; block>=0; block--)
	{
		//Generate the words one at a time
		//The first bit shifted into the LUT is selected by A[4:0] = 5'b11111.
		//The last bit is selected by A[4:0] = 5'b00000.
		for(int row=31; row>=0; row--)
		{
			//Zero out unused high order bits
			if(row >= 16)
			{
				trigger_bitstream.push_back(0);
				continue;
			}

			//Extract one bit from each bitplane and shove it into this word.
			//Trigger LUT uses inputs 64*nrow + ncol*2 +: 1
			//Dividing by two, we get LUT number 32*nrow + ncol.
			uint32_t current_word = 0;
			for(uint32_t col=0; col<32; col++)
			{
				uint32_t entry = (truth_tables[block*32 + col] >> row) & 1;
				current_word |= (entry << col);
			}

			trigger_bitstream.push_back(current_word);
		}
	}

	//Flip endianness after printing
	//LogDebug("Endian flip\n");
	FlipEndian32Array((unsigned char*)&trigger_bitstream[0], trigger_bitstream.size() * 4);

	//Debug print (in wire endianness)
	/*
	LogDebug("Trigger message\n");
	const unsigned char* p = (const unsigned char*)&trigger_bitstream[0];
	for(size_t i=0; i<trigger_bitstream.size()*4; i++)
	{
		LogDebug("%02x ", p[i]);
		if( (i & 3) == 3)
			LogDebug("\n");
	}
	*/

	if(m_uart)
	{
		//Send the header
		uint8_t op = REDTIN_LOAD_TRIGGER;
		m_uart->Write(&op, 1);

		//Send the trigger bitstream after endian correction
		if(!m_uart->Write((unsigned char*)&trigger_bitstream[0], trigger_bitstream.size() * 4))
			LogError("Failed to send bitstream to DUT\n");
		LogDebug("Bitstream size: %zu\n", trigger_bitstream.size() * 4);

		//Wait for OK result
		m_uart->Read(&op, 1);
		if(op != REDTIN_LOAD_TRIGGER)
			LogError("Bad response from LA (%02x, expected %02x)\n", op, REDTIN_LOAD_TRIGGER);
	}

	else
	{
		LogError("non-uart not implemented\n");
		//m_iface.SendDMAMessage(trigmsg);
	}
}

void RedTinLogicAnalyzer::Start()
{
	printf("continuous capture not implemented\n");
}

void RedTinLogicAnalyzer::Stop()
{

}

void RedTinLogicAnalyzer::ResetTriggerConditions()
{
	for(size_t i=0; i<m_triggers.size(); i++)
		m_triggers[i] = TRIGGER_TYPE_DONTCARE;
}

void RedTinLogicAnalyzer::SetTriggerForChannel(OscilloscopeChannel* channel, std::vector<TriggerType> triggerbits)
{
	int nstart = m_width - 1;
	for(size_t i=0; i<m_channels.size(); i++)
	{
		OscilloscopeChannel* chan = m_channels[i];

		//If the channel is procedural, skip it (no effect on the actual data)
		if(dynamic_cast<ProtocolDecoder*>(chan) != NULL)
			continue;

		int width = chan->GetWidth();
		int hi = nstart;
		int lo = nstart - width + 1;
		nstart -= width;

		//Check if we've hit the target channel, if not keep moving
		if(channel != chan)
			continue;

		//Hit - sanity-check signal itself
		if(triggerbits.size() != (size_t)width)
		{
			throw JtagExceptionWrapper(
				"Trigger array size does not match signal width",
				"");
		}

		LogDebug("Signal %s = bits %d to %d\n", chan->m_displayname.c_str(), hi, lo);

		//Copy the array
		for(size_t j=0; j<triggerbits.size(); j++)
		{
			int k = hi - j;
			m_triggers[k] = triggerbits[j];
			//printf("    trigger bit %d = %d\n", k, triggerbits[j]);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helpers for trigger truth table generation

int bit_test_pair(int state_0, int state_1, int current_1, int old_1, int current_0, int old_0)
{
	return bit_test(state_0, current_0, old_0) && bit_test(state_1, current_1, old_1);
}

int bit_test(int state, int current, int old)
{
	switch(state)
	{
		case RedTinLogicAnalyzer::TRIGGER_TYPE_LOW:
			return (!current);
		case RedTinLogicAnalyzer::TRIGGER_TYPE_HIGH:
			return (current);
		case RedTinLogicAnalyzer::TRIGGER_TYPE_RISING:
			return (current && !old);
		case RedTinLogicAnalyzer::TRIGGER_TYPE_FALLING:
			return (!current && old);
		case RedTinLogicAnalyzer::TRIGGER_TYPE_CHANGE:
			return (current != old);
		case RedTinLogicAnalyzer::TRIGGER_TYPE_DONTCARE:
			return 1;
	}
	return 0;
}

int MakeTruthTable(int state_0, int state_1)
{
	int table = 0;
	for(int current_0 = 0; current_0 <= 1; current_0 ++)
	{
		for(int current_1 = 0; current_1 <= 1; current_1 ++)
		{
			for(int old_0 = 0; old_0 <= 1; old_0 ++)
			{
				for(int old_1 = 0; old_1 <= 1; old_1 ++)
				{
					int bitnum = (current_1 << 3) | (current_0 << 2) | (old_1 << 1) | (old_0);
					int bitval = bit_test_pair(state_0, state_1, current_1, old_1, current_0, old_0);
					table |= (bitval << bitnum);
				}
			}
		}
	}
	return table;
}
