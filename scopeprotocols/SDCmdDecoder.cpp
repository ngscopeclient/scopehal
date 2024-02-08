/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
*                                                                                                                      *
* Copyright (c) 2012-2022 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Implementation of SDCmdDecoder
 */

#include "../scopehal/scopehal.h"
#include "SDCmdDecoder.h"
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SDCmdDecoder::SDCmdDecoder(const string& color)
	: PacketDecoder(color, CAT_MEMORY)
	, m_cardtypename("Card Type")
{
	CreateInput("CMD");
	CreateInput("CLK");

	m_parameters[m_cardtypename] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_cardtypename].AddEnumValue("SD", SD_GENERIC);
	m_parameters[m_cardtypename].AddEnumValue("eMMC", SD_EMMC);
	m_parameters[m_cardtypename].SetIntVal(SD_GENERIC);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

string SDCmdDecoder::GetProtocolName()
{
	return "SD Card Command Bus";
}

bool SDCmdDecoder::ValidateChannel(size_t i, StreamDescriptor stream)
{
	if(stream.m_channel == NULL)
		return false;

	if( (i < 6) && (stream.GetType() == Stream::STREAM_TYPE_DIGITAL) )
		return true;

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void SDCmdDecoder::Refresh()
{
	ClearPackets();

	//Make sure we've got valid inputs
	if(!VerifyAllInputsOK())
	{
		SetData(NULL, 0);
		return;
	}

	//Sample the input data
	auto cmd = GetInputWaveform(0);
	auto clk = GetInputWaveform(1);
	cmd->PrepareForCpuAccess();
	clk->PrepareForCpuAccess();

	SparseDigitalWaveform dcmd;
	dcmd.PrepareForCpuAccess();
	SampleOnRisingEdgesBase(cmd, clk, dcmd);
	size_t len = dcmd.size();

	//Create the capture
	auto cap = new SDCmdWaveform(m_parameters[m_cardtypename]);
	cap->m_timescale = 1;
	cap->m_startTimestamp = clk->m_startTimestamp;
	cap->m_startFemtoseconds = clk->m_startFemtoseconds;
	SetData(cap, 0);

	enum
	{
		STATE_IDLE,
		STATE_TYPE,
		STATE_COMMAND_HEADER,
		STATE_COMMAND_BODY,
		STATE_CRC,
		STATE_STOP,

		STATE_RESPONSE_HEADER,
		STATE_RESPONSE_BODY
	} state = STATE_IDLE;

	int64_t tstart = 0;
	int nbit = 0;
	uint32_t data = 0;
	uint32_t extdata[4] = {0};

	//Reference: SD Physical Layer Simplified Specification v.8.00

	int last_cmd = 0;
	bool app_cmd = false;
	Packet* pack = NULL;
	for(size_t i=0; i<len; i++)
	{
		bool b = dcmd.m_samples[i];
		int64_t off = dcmd.m_offsets[i];		//no need to multiply by timescale
		int64_t dur = dcmd.m_durations[i];		//because SampleOnRisingEdges() always uses 1fs timesteps
		int64_t end = off + dur;

		switch(state)
		{
			//Wait for a start bit
			case STATE_IDLE:
				if(!b)
				{
					tstart = off;
					state = STATE_TYPE;

					//Create a new packet. If we already have an incomplete one that got aborted, reset it
					if(pack)
					{
						pack->m_data.clear();
						pack->m_headers.clear();
					}
					else
						pack = new Packet;

					pack->m_offset = dcmd.m_offsets[i];
					pack->m_len = 0;
				}
				break;

			//Read the type bit
			//1 = command, 0 = response
			case STATE_TYPE:
				cap->m_offsets.push_back(tstart);
				cap->m_durations.push_back(end - tstart);
				cap->m_samples.push_back(SDCmdSymbol(SDCmdSymbol::TYPE_HEADER, b));

				tstart = end;
				nbit = 0;
				data = 0;
				if(b)
				{
					state = STATE_COMMAND_HEADER;
					pack->m_headers["Type"] = "Command";
				}
				else
				{
					state = STATE_RESPONSE_HEADER;
					pack->m_headers["Type"] = "Reply";
				}

				break;

			//Start a command or reply packet
			//Read command index (6 bits)
			case STATE_COMMAND_HEADER:
			case STATE_RESPONSE_HEADER:
				data = (data << 1) | b;
				nbit ++;
				if(nbit == 6)
				{
					cap->m_offsets.push_back(tstart);
					cap->m_durations.push_back(end - tstart);

					//If the last command was CMD55 (APP_CMD) then add 100.
					//We code ACMD1 as 101, etc.
					if(state == STATE_RESPONSE_HEADER)
					{
						if(app_cmd)
							data += 100;
					}
					else
					{
						if(data == 55)
							app_cmd = true;
						else if(app_cmd)
						{
							data += 100;
							app_cmd = false;
						}
					}

					//Save the command code so we know how to parse replies
					if(state == STATE_COMMAND_HEADER)
						last_cmd = data;

					cap->m_samples.push_back(SDCmdSymbol(SDCmdSymbol::TYPE_COMMAND, data));

					pack->m_headers["Command"] = cap->GetText(cap->m_samples.size()-1);

					if(last_cmd >= 100)
						pack->m_headers["Code"] = string("ACMD") + to_string(last_cmd - 100);
					else
						pack->m_headers["Code"] = string("CMD") + to_string(last_cmd);

					//Set packet color based on command
					if(state == STATE_RESPONSE_HEADER)
						pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_STATUS];
					else
					{
						switch(data)
						{
							//WRITE_BLOCK, WRITE_MULTIPLE_BLOCK
							case 24:
							case 25:
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_WRITE];
								break;

							//READ_SINGLE_BLOCK, READ_MULTIPLE_BLOCK
							case 17:
							case 18:
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_DATA_READ];
								break;

							//Default everything else to "control"
							default:
								pack->m_displayBackgroundColor = m_backgroundColors[PROTO_COLOR_CONTROL];
								break;
						}
					}

					data = 0;
					nbit = 0;
					tstart = end;
					if(state == STATE_COMMAND_HEADER)
						state = STATE_COMMAND_BODY;
					else
						state = STATE_RESPONSE_BODY;
				}
				break;

			//Read arguments for a command packet
			case STATE_COMMAND_BODY:
				data = (data << 1) | b;
				nbit ++;

				if(nbit == 32)
				{
					cap->m_offsets.push_back(tstart);
					cap->m_durations.push_back(end - tstart);
					cap->m_samples.push_back(SDCmdSymbol(SDCmdSymbol::TYPE_COMMAND_ARGS, data));

					data = 0;
					nbit = 0;
					tstart = end;
					state = STATE_CRC;

					pack->m_headers["Info"] = cap->GetText(cap->m_samples.size()-1);
				}
				break;

			//Read arguments for a response packet
			case STATE_RESPONSE_BODY:
				{
					//Figure out the expected reply format (4.7.4)
					data = (data << 1) | b;
					nbit ++;

					//CMD2 has a 128-bit response with no CRC
					if(last_cmd == 2)
					{
						if(nbit == 32)
							extdata[0] = data;
						if(nbit == 64)
							extdata[1] = data;
						if(nbit == 96)
							extdata[2] = data;
						if(nbit == 128)
						{
							extdata[3] = data;

							cap->m_offsets.push_back(tstart);
							cap->m_durations.push_back(end - tstart);
							cap->m_samples.push_back(SDCmdSymbol(SDCmdSymbol::TYPE_RESPONSE_ARGS,
								extdata[0], extdata[1], extdata[2], extdata[3]));

							pack->m_headers["Info"] = cap->GetText(cap->m_samples.size()-1);

							//no CRC
							//stop bit is parsed as last data bit
							state = STATE_IDLE;

							//end the packet now
							m_packets.push_back(pack);
							pack = NULL;
						}
					}
					else if(nbit == 32)
					{
						cap->m_offsets.push_back(tstart);
						cap->m_durations.push_back(end - tstart);
						cap->m_samples.push_back(SDCmdSymbol(SDCmdSymbol::TYPE_RESPONSE_ARGS, data));

						data = 0;
						nbit = 0;
						tstart = end;
						state = STATE_CRC;

						pack->m_headers["Info"] = cap->GetText(cap->m_samples.size()-1);
					}

				}
				break;

			//Reads the CRC
			//ACMD41 response always has 0x7F here for some reason and not a real CRC (4.9.4)
			case STATE_CRC:
				data = (data << 1) | b;
				nbit ++;
				if(nbit == 7)
				{
					//TODO: verify the CRC
					cap->m_offsets.push_back(tstart);
					cap->m_durations.push_back(end - tstart);
					cap->m_samples.push_back(SDCmdSymbol(SDCmdSymbol::TYPE_CRC_OK, data));

					state = STATE_STOP;
				}
				break;

			//Look for stop bit
			case STATE_STOP:
				if(b != 1)
				{
					cap->m_offsets.push_back(off);
					cap->m_durations.push_back(end);
					cap->m_samples.push_back(SDCmdSymbol(SDCmdSymbol::TYPE_ERROR, b));
				}

				pack->m_len = end - pack->m_offset;
				m_packets.push_back(pack);
				pack = NULL;

				state = STATE_IDLE;
				break;

			default:
				break;
		}
	}

	if(pack)
		delete pack;

	cap->MarkModifiedFromCpu();
}

bool SDCmdDecoder::GetShowDataColumn()
{
	return false;
}

std::string SDCmdWaveform::GetColor(size_t i)
{
	auto s = m_samples[i];
	switch(s.m_stype)
	{
		case SDCmdSymbol::TYPE_HEADER:
			return StandardColors::colors[StandardColors::COLOR_ADDRESS];

		case SDCmdSymbol::TYPE_COMMAND:
			return StandardColors::colors[StandardColors::COLOR_CONTROL];

		case SDCmdSymbol::TYPE_COMMAND_ARGS:
		case SDCmdSymbol::TYPE_RESPONSE_ARGS:
			return StandardColors::colors[StandardColors::COLOR_DATA];

		case SDCmdSymbol::TYPE_CRC_OK:
			return StandardColors::colors[StandardColors::COLOR_CHECKSUM_OK];

		case SDCmdSymbol::TYPE_CRC_BAD:
			return StandardColors::colors[StandardColors::COLOR_CHECKSUM_BAD];

		case SDCmdSymbol::TYPE_ERROR:
		default:
			return StandardColors::colors[StandardColors::COLOR_ERROR];
	}
}

string SDCmdWaveform::GetText(size_t i)
{
	char tmp[128];

	SDCmdDecoder::CardType cardtype = static_cast<SDCmdDecoder::CardType>(m_cardTypeParam.GetIntVal());

	auto s = m_samples[i];
	switch(s.m_stype)
	{
		case SDCmdSymbol::TYPE_HEADER:
			if(s.m_data)
				return "CMD";
			else
				return "REPLY";

		case SDCmdSymbol::TYPE_COMMAND:
			{
				//We code ACMDs at offset 100
				string ret;
				if(s.m_data == 155)
					ret = string("CMD55");
				else if(s.m_data >= 100)
					ret = string("ACMD") + to_string(s.m_data - 100);
				else
					ret = string("CMD") + to_string(s.m_data);

				switch(s.m_data)
				{
					case 0:		return "GO_IDLE_STATE";

					//CMD1 reserved for SDIO
					//eMMC uses it for SEND_OP_COND
					case 1:
						if(cardtype == SDCmdDecoder::SD_EMMC)
							return "SEND_OP_COND";
						else
							return ret;

					case 2:		return "ALL_SEND_CID";
					case 3:		return "SEND_RELATIVE_ADDR";
					case 4:		return "SET_DSR";

					//CMD5 reserved for SDIO.
					//eMMC uses it for SLEEP/AWAKE mode
					case 5:
						if(cardtype == SDCmdDecoder::SD_EMMC)
							return "SLEEP_AWAKE";
						break;

					case 6:
						if(cardtype == SDCmdDecoder::SD_EMMC)
							return "SWITCH";
						else
							return "SET_BUS_WIDTH";

					case 7:		return "SELECT_DESELECT_CARD";

					case 8:
						if(cardtype == SDCmdDecoder::SD_EMMC)
							return "SEND_EXT_CSD";
						else
							return "SEND_IF_COND";

					case 9:		return "SEND_CSD";
					case 10:	return "SEND_CID";
					case 11:	return "VOLTAGE_SWITCH";
					case 12:	return "STOP_TRANSMISSION";
					case 13:	return "SEND_STATUS";

					//CMD14 reserved
					//eMMC uses it for bus testing
					case 14:
						if(cardtype == SDCmdDecoder::SD_EMMC)
							return "BUSTEST_R";
						break;

					case 15:	return "GO_INACTIVE_STATE";
					case 16:	return "SET_BLOCKLEN";
					case 17:	return "READ_SINGLE_BLOCK";
					case 18:	return "READ_MULTIPLE_BLOCK";
					case 19:	return "SEND_TUNING_BLOCK";
					case 20:	return "SPEED_CLASS_CONTROL";

					//CMD21 reserved
					//eMMC uses it for link training in HS200 mode
					case 21:
						if(cardtype == SDCmdDecoder::SD_EMMC)
							return "SEND_TUNING_BLOCK";
						break;

					case 22:	return "ADDRESS_EXTENSION";
					case 23:	return "SET_BLOCK_COUNT";
					case 24:	return "WRITE_BLOCK";
					case 25:	return "WRITE_MULTIPLE_BLOCK";
					//CMD26 reserved for manufacturer
					case 27:	return "PROGRAM_CSD";
					case 28:	return "SET_WRITE_PROT";
					case 29:	return "CLR_WRITE_PROT";
					case 30:	return "SEND_WRITE_PROT";
					//CMD31 reserved
					case 32:	return "ERASE_WR_BLK_START";
					case 33:	return "ERASE_WR_BLK_END";
					//CMD34-38 TODO
					case 38:	return "ERASE";
					//CMD39 reserved
					//CMD40 defined by "DPS Spec" whatever that is
					//CMD41 reserved
					case 42:	return "LOCK_UNLOCK";

					//CMD52-54 reserved for SDIO mode

					case 55:
					case 155:	//What is the "RCA"?
						return "APP_CMD";

					case 56:	return "GEN_CMD";
					//CMD60-63 reserved for manufacturer

					//Parse CMD63 response depending on what the previous command was
					case 63:
					{
						//Should be a command at i-4
						if(i < 4)
							return "ERROR";
						return GetText(i-4);
					}

					//ACMD1-5 reserved
					case 106:	return "SET_BUS_WIDTH";
					//ACMD7-12 reserved
					case 113:	return "SD_STATUS";
					//ACMD14-16 reserved for DPS Specification
					//ACMD17 reserved
					//ACMD18 reserved for SD Security
					//ACMD19-21 reserved
					case 122:	return "SEND_NUM_WR_BLOCKS";
					case 123:	return "SET_WR_BLK_ERASE_COUNT";
					//ACMD24 reserved
					//ACMD25-26 reserved for SD Security
					//ACMD27 "shall not use"
					//ACMD28 reserved for DPS Specification
					//ACMD29 reserved
					//ACMD30-35 reserved for Security Specification
					//ACMD36-37 reserved
					//ACMD38 reserved for SD Security
					//ACMD39-40 reserved
					case 141:	return "SEND_OP_COND";
					case 142:	return "SET_CLR_CARD_DETECT";
					//ACMD43-49 reserved for SD Security
					//ACMD50?
					case 151:	return "SEND_SCR";
					//ACMD52-54 reserved for SD Security
					//ACMD55 is just CMD55
					//ACMD56-59 reserved for SD Security

					default:
						return ret;
				}
			}
			break;

		case SDCmdSymbol::TYPE_COMMAND_ARGS:
			{
				//Look up the command
				//(TYPE_COMMAND_ARGS can never be the first sample in a waveform)
				auto type = m_samples[i-1].m_data;

				snprintf(tmp, sizeof(tmp), "%08x", s.m_data);
				string ret = tmp;

				switch(type)
				{
					//No arguments
					case 0:
					case 1:
					case 2:
						return "";

					//CMD5 SLEEP/AWAKE
					case 5:
						if(cardtype == SDCmdDecoder::SD_EMMC)
						{
							if(s.m_data & 0x8000)
								snprintf(tmp, sizeof(tmp), "RCA=%04x SLEEP", s.m_data >> 16);
							else
								snprintf(tmp, sizeof(tmp), "RCA=%04x WAKE", s.m_data >> 16);
							ret = tmp;
						}
						break;

					//CMD6 SWITCH
					case 6:
						if(cardtype == SDCmdDecoder::SD_EMMC)
						{
							int access = (s.m_data >> 24) & 3;
							int cmdset = s.m_data & 3;
							int index = (s.m_data >> 16) & 0xff;
							int value = (s.m_data >> 8) & 0xff;

							//EXT_CSD registers (emmc spec section 7.4)
							//TODO: binary vs linear search
							const char* regname = NULL;
							static const struct
							{
								int index;
								const char* name;
							} extregs[] =
							{
								{15, "CMDQ_MODE_EN"},
								{16, "SECURE_REMOVAL_TYPE"},
								{17, "PRODUCT_STATE_AWARENESS_ENABLEMENT"},
								//18-21 MAX_PRE_LOADING_DATA_SIZE
								//22-25 PRE_LOADING_DATA_SIZE
								{26, "FFU_STATUS"},
								{29, "MODE_OPERATION_CODES"},
								{30, "MODE_CONIG"},
								{31, "BARRIER_CTRL"},
								{32, "FLUSH_CACHE"},
								{33, "CACHE_CTRL"},
								{34, "POWER_OFF_NOTIFICATION"},
								{35, "PACKED_FAILURE_INDEX"},
								{36, "PACKED_COMMAND_STATUS"},
								//37-51 CONTEXT_CONF
								{52, "EXT_PARTITIONS_ATTRIBUTE_0"},
								{53, "EXT_PARTITIONS_ATTRIBUTE_1"},
								{54, "EXCEPTION_EVENTS_STATUS_0"},
								{55, "EXCEPTION_EVENTS_STATUS_1"},
								{56, "EXCEPTION_EVENTS_CTRL_0"},
								{57, "EXCEPTION_EVENTS_CTRL_1"},
								{58, "DYNCAP_NEEDED"},
								{59, "CLASS_6_CTRL"},
								{60, "INI_TIMEOUT_EMU"},
								{61, "DATA_SECTOR_SIZE"},
								{62, "USE_NATIVE_SECTOR"},
								{63, "NATIVE_SECTOR_SIZE"},
								//64-127 vendor specific
								{130, "PROGRAM_CID_CSD_DDR_SUPPORT"},
								{131, "PERIODIC_WAKEUP"},
								{132, "TCASE_SUPPORT"},
								{133, "PRODUCTION_STATE_AWARENESS"},
								{134, "SEC_BAD_BLK_MGMNT"},
								{136, "ENH_START_ADDR_0"},
								{137, "ENH_START_ADDR_1"},
								{138, "ENH_START_ADDR_2"},
								{139, "ENH_START_ADDR_3"},
								//140-142 ENH_SIZE_MULT
								//143-154 GP_SIZE_MULT_GP0-3
								{155, "PARTITION_SETTING_COMPLETED"},
								{156, "PARTITIONS_ATTRIBUTE"},
								//157-159 MAX_ENH_SIZE_MULT
								{160, "PARTITIONING_SUPPORT"},
								{161, "HPI_MGMT"},
								{162, "RST_N_FUNCTION"},
								{163, "BKOPS_EN"},
								{164, "BKOPS_START"},
								{165, "SANITIZE_START"},
								{166, "WR_REL_PARAM"},
								{167, "WR_REL_SET"},
								{168, "RPMB_SIZE_MULT"},
								{169, "FW_CONFIG"},
								{171, "USER_WP"},
								{173, "BOOT_WP"},
								{174, "BOOT_WP_STATUS"},
								{175, "ERASE_GROUP_DEV"},
								{177, "BOOT_BUS_CONDITIONS"},
								{178, "BOOT_CONFIG_PROT"},
								{179, "PARTITION_CONFIG"},
								{181, "ERASED_MEM_CONT"},
								{183, "BUS_WIDTH"},
								{184, "STROBE_SUPPORT"},
								{185, "HS_TIMING"},
								{187, "POWER_CLASS"},
								{189, "CMD_SET_REV"},
								{191, "CMD_SET"},
								{192, "EXT_CSD_REV"},
								{194, "CSD_STRUCTURE"},
								{196, "DEVICE_TYPE"},
								{197, "DRIVER_STRENGTH"},
								{198, "OUT_OF_INTERRUPT_TIME"},
								{199, "PARTITION_SWITCH_TIME"},
								//200-203 PWR_CL_ff_vvv
								//205-210 MIN_PERF_a_b_ff
								{211, "SEC_WP_SUPPORT"},
								//212-215 SEC_COUNT
								{216, "SLEEP_NOTIFICATION_TIME"},
								{217, "S_A_TIMEOUT"},
								{218, "PRODUCTION_STATE_AWARENESS_TIMEOUT"},
								{219, "S_C_VCCQ"},
								{220, "S_C_VCC"},
								{221, "HC_WP_GRP_SIZE"},
								{222, "REL_WR_SEC_C"},
								{223, "ERASE_TIMEOUT_MULT"},
								{224, "HC_ERASE_GRP_SIZE"},
								{225, "ACC_SIZE"},
								{226, "BOOT_SIZE_MULT"},
								//227 reserved
								{228, "BOOT_INFO"},
								{229, "SEC_TRIM_MULT"},
								{230, "SEC_ERASE_MULT"},
								{231, "SEC_FEATURE_SUPPORT"},
								{232, "TRIM_MULT"},
								//234-235 MIN_PERF_DDR_a_b_ff
								//236-237 PWR_CL_ff_vvv
								//238-239 PWR_CL_DDR_ff_vvv
								{240, "CACHE_FLUSH_POLICY"},
								{241, "INI_TIMEOUT_AP"},
								//242-245 CORRECTLY_PRG_SECTORS_NUM
								{246, "BKOPS_STATUS"},
								{247, "POWER_OFF_LONG_TIME"},
								{248, "GENERIC_CMD6_TIME"},
								//249-252 CACHE_SIZE
								//253 PWR_CL_DDR_ff_vvv
								//254-261 FIRMWARE_VERSION
								//262-263 DEVICE_VERSION
								{264, "OPTIMAL_TRIM_UNIT_SIZE"},
								{265, "OPTIMAL_WRITE_SIZE"},
								{266, "OPTIMAL_READ_SIZE"},
								{267, "PRE_EOL_INFO"},
								{268, "DEVICE_LIFE_TIME_EST_TYPE_A"},
								{269, "DEVICE_LIFE_TIME_EST_TYPE_B"},
								//270-301 VENDOR_PROPRIETARY_HEALTH_REPORT
								//302-305 NUMBER_OF_FW_SECTORS_CORRECTLY_PROGRAMMED
								{307, "CMDQ_DEPTH"},
								{308, "CMDQ_SUPPORT"},
								//309-485 reserved
								{486, "BARRIER_SUPPORT"},
								//487-490 FFU_ARG
								{491, "OPERATION_CODES_TIMEOUT"},
								{492, "FFU_FEATURES"},
								{493, "SUPPORTED_MODES"},
								{494, "EXT_SUPPORT"},
								{495, "LARGE_UNIT_SIZE_M1"},
								{496, "CONTEXT_CAPABILITIES"},
								{497, "TAG_RES_SIZE"},
								{498, "TAG_UNIT_SIZE"},
								{499, "DATA_TAG_SUPPORT"},
								{500, "MAX_PACKED_WRITES"},
								{501, "MAX_PACKED_READS"},
								{502, "BKOPS_SUPPORT"},
								{503, "HPI_FEATURES"},
								{504, "S_CMD_SET"},
								{505, "EXT_SECURITY_ERR"}
							};
							for(size_t j=0; j<sizeof(extregs)/sizeof(extregs[0]); j++)
							{
								if(extregs[j].index == index)
								{
									regname = extregs[j].name;
									break;
								}
							}

							switch(access)
							{
								case 0:
									snprintf(tmp, sizeof(tmp), "CommandSet %d", cmdset);
									break;
								case 1:
									if(regname)
										snprintf(tmp, sizeof(tmp), "%s |= 0x%02x", regname, value);
									else
										snprintf(tmp, sizeof(tmp), "EXT_CSD[%d] |= 0x%02x", index, value);
									break;
								case 2:
									if(regname)
										snprintf(tmp, sizeof(tmp), "%s &= ~0x%02x", regname, value);
									else
										snprintf(tmp, sizeof(tmp), "EXT_CSD[%d] &= ~0x%02x", index, value);
									break;
								case 3:
									if(regname)
										snprintf(tmp, sizeof(tmp), "%s = 0x%02x", regname, value);
									else
										snprintf(tmp, sizeof(tmp), "EXT_CSD[%d] = 0x%02x", index, value);
									break;
							}
							ret = tmp;
						}
						//TODO SDIO decoding
						break;

					//CMD7 Select/Deselect Card
					case 7:
						snprintf(tmp, sizeof(tmp), "RCA=%04x", s.m_data >> 16);
						ret = tmp;
						break;

					//For eMMC: SEND_EXT_CSD (no arguments)
					//For SD: CMD8 Send Interface Condition (4.3.13)
					case 8:
						if(cardtype == SDCmdDecoder::SD_EMMC)
							ret = "";
						else
						{
							snprintf(tmp, sizeof(tmp), "%02x", s.m_data & 0xff);
							ret = string("Check ") + tmp;

							if(s.m_data & 0x2000)
								ret += " 1V2? ";
							if(s.m_data & 0x1000)
								ret += " PCIe? ";

							int voltage = (s.m_data >> 8) & 0xf;
							if(voltage == 1)
								ret += " 3V3";
							else
								ret += " Vunknown";
						}
						break;

					//CMD9 SEND_CSD
					case 9:
						if(cardtype == SDCmdDecoder::SD_EMMC)
						{
							snprintf(tmp, sizeof(tmp), "RCA=%04x", s.m_data >> 16);
							ret = tmp;
						}
						break;

					//CMD16 Set Block Length
					case 16:
						snprintf(tmp, sizeof(tmp), "Block size = %d", s.m_data);
						ret = tmp;
						break;

					//CMD17 Read Single Block
					//CMD18 Read Multiple Block
					//CMD24 Write Block
					//CMD25 Write Multiple Block
					case 17:
					case 18:
					case 24:
					case 25:
						snprintf(tmp, sizeof(tmp), "LBA = %08x", s.m_data);
						ret = tmp;
						break;

					//ACMD6 SET_BUS_WIDTH
					case 106:
						if( (s.m_data & 3) == 0)
							ret = "x1";
						else if( (s.m_data & 3) == 2)
							ret = "x4";
						else
							ret = "Invalid bus width";
						break;

					//ACMD41 SD_SEND_OP_COND
					//30 HCS
					//28 XPC
					//24 S18R
					//23:0 VDD range
					case 141:
						{
							ret = "";
							if(s.m_data & 0x40000000)
								ret += "HCS ";
							if(s.m_data & 0x10000000)
								ret += "XPC ";

							//VDD voltage window
							uint32_t window = s.m_data & 0xffffff;
							if(window == 0)
								ret += "Voltage?";
							else
							{
								float vmax = 0;
								float vmin = 3.6;

								if(window & 0x00800000)
								{
									vmax = max(vmax, 3.6f);
									vmin = min(vmin, 3.5f);
								}
								if(window & 0x00400000)
								{
									vmax = max(vmax, 3.5f);
									vmin = min(vmin, 3.4f);
								}
								if(window & 0x00200000)
								{
									vmax = max(vmax, 3.4f);
									vmin = min(vmin, 3.3f);
								}
								if(window & 0x00100000)
								{
									vmax = max(vmax, 3.3f);
									vmin = min(vmin, 3.2f);
								}
								if(window & 0x00080000)
								{
									vmax = max(vmax, 3.2f);
									vmin = min(vmin, 3.1f);
								}
								if(window & 0x00040000)
								{
									vmax = max(vmax, 3.1f);
									vmin = min(vmin, 3.0f);
								}
								if(window & 0x00020000)
								{
									vmax = max(vmax, 3.0f);
									vmin = min(vmin, 2.9f);
								}
								if(window & 0x00010000)
								{
									vmax = max(vmax, 2.9f);
									vmin = min(vmin, 2.8f);
								}
								if(window & 0x00008000)
								{
									vmax = max(vmax, 2.8f);
									vmin = min(vmin, 2.7f);
								}

								snprintf(tmp, sizeof(tmp), "Vdd = %.1f - %.1f", vmin, vmax);
								ret += tmp;
							}
						}
						break;

					//ACMD42 SET_CLR_CARD_DETECT
					case 142:
						if(s.m_data & 1)
							ret = "CD/DAT3 pullup enable";
						else
							ret = "CD/DAT3 pullup disable";

					default:
						break;
				}

				return ret;
			}

		case SDCmdSymbol::TYPE_RESPONSE_ARGS:
			{
				//Look up the command
				//(TYPE_RESPONSE_ARGS can never be the first sample in a waveform)
				auto type = m_samples[i-1].m_data;

				//Back up by 5 (previous command) and figure out what we got
				if(i >= 5)
				{
					type = m_samples[i-5].m_data;
				}

				snprintf(tmp, sizeof(tmp), "%08x", s.m_data);
				string ret = tmp;

				switch(type)
				{
					//R3 (OCR register)
					//(only valid for eMMC)
					case 1:

						//Card ready
						if(s.m_data & 0x80000000)
						{
							int access = (s.m_data >> 29) & 3;
							if(access == 0)
								ret = "ByteAcc ";
							else if(access == 2)
								ret = "SectorAcc ";
							else
								ret = "InvalidAcc ";
							if(s.m_data & 0x80)
								ret += "1V8 ";
						}

						//Card busy, still initializing
						else
							ret = "BUSY";

						break;

					//4.9.3 R2 (CID or CSD register)
					case 2:
						{
							snprintf(tmp, sizeof(tmp), "%08x %08x %08x %08x ",
								s.m_data, s.m_extdata1, s.m_extdata2, s.m_extdata3);
							ret = tmp;
						}
						break;

					//4.9.5 R6 (Published RCA response)
					case 3:
						{
							snprintf(tmp, sizeof(tmp), "RCA=%04x ", s.m_data >> 16);
							ret = tmp;

							//Low 16 bits have same meaning as 23, 22, 19, 12:0 from normal card status

							if(s.m_data & 0x00008000)
								ret += "COM_CRC_ERROR ";
							if(s.m_data & 0x00004000)
								ret += "ILLEGAL_COMMAND ";
							if(s.m_data & 0x00002000)
								ret += "ERROR ";
							if(s.m_data & 0x00000100)
								ret += "READY_FOR_DATA ";
							if(s.m_data & 0x00000040)
								ret += "FX_EVENT ";
							if(s.m_data & 0x00000020)
								ret += "APP_CMD ";
							if(s.m_data & 0x00000010)
								ret += "RESERVED_SDIO ";
							if(s.m_data & 0x00000008)
								ret += "AKE_SEQ_ERR ";
							if(s.m_data & 0x00060084)
								ret += "RESERVED ";

							//12:9 CURRENT_STATE
							uint32_t state = (s.m_data >> 9) & 0xf;
							switch(state)
							{
								case 0:
									ret += "idle";
									break;

								case 1:
									ret += "ready";
									break;

								case 2:
									ret += "ident";
									break;

								case 3:
									ret += "stby";
									break;

								case 4:
									ret += "tran";
									break;

								case 5:
									ret += "data";
									break;

								case 6:
									ret += "rcv";
									break;

								case 7:
									ret += "prg";
									break;

								case 8:
									ret += "dis";
									break;

								default:
									ret += "reserved_state";
							}
						}
						break;


					//SD: R7 Card Interface Condition (4.9.6)
					//eMMC: Extended CSD (reply is a block of data on the data pins)
					case 8:
						if(cardtype == SDCmdDecoder::SD_EMMC)
							ret = "";
						else
						{
							snprintf(tmp, sizeof(tmp), "%02x", s.m_data & 0xff);
							ret = string("Check ") + tmp;

							if(s.m_data & 0x2000)
								ret += " 1V2 ";
							if(s.m_data & 0x1000)
								ret += " PCIe ";

							int voltage = (s.m_data >> 8) & 0xf;
							if(voltage == 1)
								ret += " 3V3";
							else
								ret += " Vunknown";

						}
						break;

					//R3 OCR Register (4.9.4, 5.1). Operation Conditions Register Register :)
					case 141:
						{
							ret = "";
							if( (s.m_data & 0x80000000) == 0)
								ret += "BUSY ";

							//CCS bit is only valid after powerup is complete
							else
							{
								if(s.m_data & 0x08000000)
									ret += "UC";
								else if(s.m_data & 0x40000000)
									ret += "HC/XC ";
								else
									ret += "SC ";
							}

							if(s.m_data & 0x01000000)
								ret += "S18A ";

							//VDD voltage window
							uint32_t window = s.m_data & 0xffffff;
							if(window == 0)
								ret += "Voltage?";
							else
							{
								float vmax = 0;
								float vmin = 3.6;

								if(window & 0x00800000)
								{
									vmax = max(vmax, 3.6f);
									vmin = min(vmin, 3.5f);
								}
								if(window & 0x00400000)
								{
									vmax = max(vmax, 3.5f);
									vmin = min(vmin, 3.4f);
								}
								if(window & 0x00200000)
								{
									vmax = max(vmax, 3.4f);
									vmin = min(vmin, 3.3f);
								}
								if(window & 0x00100000)
								{
									vmax = max(vmax, 3.3f);
									vmin = min(vmin, 3.2f);
								}
								if(window & 0x00080000)
								{
									vmax = max(vmax, 3.2f);
									vmin = min(vmin, 3.1f);
								}
								if(window & 0x00040000)
								{
									vmax = max(vmax, 3.1f);
									vmin = min(vmin, 3.0f);
								}
								if(window & 0x00020000)
								{
									vmax = max(vmax, 3.0f);
									vmin = min(vmin, 2.9f);
								}
								if(window & 0x00010000)
								{
									vmax = max(vmax, 2.9f);
									vmin = min(vmin, 2.8f);
								}
								if(window & 0x00008000)
								{
									vmax = max(vmax, 2.8f);
									vmin = min(vmin, 2.7f);
								}

								snprintf(tmp, sizeof(tmp), "Vdd = %.1f - %.1f", vmin, vmax);
								ret += tmp;
							}
						}
						break;

					//Parse anything else as R1 Card Status (4.10)
					default:
						{
							ret = "";
							if(s.m_data & 0x80000000)
								ret += "OUT_OF_RANGE ";
							if(s.m_data & 0x40000000)
								ret += "ADDRESS_ERROR ";
							if(s.m_data & 0x20000000)
								ret += "BLOCK_LEN_ERROR ";
							if(s.m_data & 0x10000000)
								ret += "ERASE_SEQ_ERROR ";
							if(s.m_data & 0x08000000)
								ret += "ERASE_PARAM ";
							if(s.m_data & 0x04000000)
								ret += "WP_VIOLATION ";
							if(s.m_data & 0x02000000)
								ret += "CARD_IS_LOCKED ";
							if(s.m_data & 0x01000000)
								ret += "LOCK_UNLOCK_FAILED ";
							if(s.m_data & 0x00800000)
								ret += "COM_CRC_ERROR ";
							if(s.m_data & 0x00400000)
								ret += "ILLEGAL_COMMAND ";
							if(s.m_data & 0x00200000)
								ret += "CARD_ECC_FAILED ";
							if(s.m_data & 0x00100000)
								ret += "CC_ERROR ";
							if(s.m_data & 0x00080000)
								ret += "ERROR ";
							if(s.m_data & 0x00010000)
								ret += "CSD_OVERWRITE ";
							if(s.m_data & 0x00008000)
								ret += "WP_ERASE_SKIP ";
							if(s.m_data & 0x00004000)
								ret += "CARD_ECC_DISABLED ";
							if(s.m_data & 0x00002000)
								ret += "ERASE_RESET ";
							if(s.m_data & 0x00000100)
								ret += "READY_FOR_DATA ";
							if(s.m_data & 0x00000040)
								ret += "FX_EVENT ";
							if(s.m_data & 0x00000020)
								ret += "APP_CMD ";
							if(s.m_data & 0x00000010)
								ret += "RESERVED_SDIO ";
							if(s.m_data & 0x00000008)
								ret += "AKE_SEQ_ERR ";
							if(s.m_data & 0x00060084)
								ret += "RESERVED ";

							//12:9 CURRENT_STATE
							uint32_t state = (s.m_data >> 9) & 0xf;
							switch(state)
							{
								case 0:
									ret += "idle";
									break;

								case 1:
									ret += "ready";
									break;

								case 2:
									ret += "ident";
									break;

								case 3:
									ret += "stby";
									break;

								case 4:
									ret += "tran";
									break;

								case 5:
									ret += "data";
									break;

								case 6:
									ret += "rcv";
									break;

								case 7:
									ret += "prg";
									break;

								case 8:
									ret += "dis";
									break;

								default:
									ret += "reserved_state";
							}
						}
						break;
				}

				return ret;
			}

		case SDCmdSymbol::TYPE_CRC_OK:
		case SDCmdSymbol::TYPE_CRC_BAD:
			snprintf(tmp, sizeof(tmp), "CRC: %02x", s.m_data);
			return string(tmp);

		case SDCmdSymbol::TYPE_ERROR:
		default:
			return "ERROR";
	}

	return "";
}

vector<string> SDCmdDecoder::GetHeaders()
{
	vector<string> ret;
	ret.push_back("Type");
	ret.push_back("Code");
	ret.push_back("Command");
	ret.push_back("Info");
	return ret;
}


bool SDCmdDecoder::CanMerge(Packet* first, Packet* cur, Packet* next)
{
	auto& firstcode = first->m_headers["Code"];
	auto& curcode = cur->m_headers["Code"];
	auto& nextcode = next->m_headers["Code"];
	auto& firstinfo = first->m_headers["Info"];
	//auto& curinfo = cur->m_headers["Info"];
	auto& nextinfo = next->m_headers["Info"];
	bool curcmd = cur->m_headers["Type"] == "Command";
	bool curreply = !curcmd;
	bool nextcmd = next->m_headers["Type"] == "Command";
	bool nextreply = !nextcmd;

	//Merge reply with the preceding command
	if( curcmd && nextreply )
		return true;

	//If the previous is a CMD55 reply, we can merge the ACMD request with it
	if(curreply && (curcode == "CMD55") && nextcmd)
		return true;

	//If the previous is an ACMD41 reply, and this is an ACMD request, merge the powerup polling
	//FIXME: this will falsely merge other ACMDs after!!
	if( curreply && (curcode == "ACMD41") && nextcmd && (nextcode == "CMD55") )
		return true;

	//Merge all command/reply groups for CMD1 (SEND_OP_COND)
	if( (curcode == "CMD1") && (nextcode == "CMD1") )
		return true;

	//Merge all command/reply groups for CMD13 (SEND_STATUS)
	if( (firstcode == "CMD13") && (nextcode == "CMD13") )
	{
		//Always merge replies
		if(nextreply)
			return true;

		//Commands must have same argument (polling same register)
		else if(firstinfo == nextinfo)
			return true;
	}

	//Merge CMD12 (STOP_TRANSMISSION) with the previous CMD18 (READ_MULTIPLE_BLOCK) or CMD25 (WRITE_MULTIPLE_BLOCK)
	if( ( (curcode == "CMD18") || (curcode == "CMD25") ) && (nextcode == "CMD12") )
		return true;

	return false;
}

Packet* SDCmdDecoder::CreateMergedHeader(Packet* pack, size_t i)
{
	if(pack->m_headers["Type"] == "Command")
	{
		Packet* ret = new Packet;
		ret->m_offset = pack->m_offset;
		ret->m_len = pack->m_len;

		//Default to copying everything
		auto code = pack->m_headers["Code"];
		ret->m_headers["Type"] = "Command";
		ret->m_headers["Code"] = code;
		ret->m_headers["Command"] = pack->m_headers["Command"];
		ret->m_displayBackgroundColor = pack->m_displayBackgroundColor;
		ret->m_headers["Info"] = pack->m_headers["Info"];

		//If the header is a CMD55 packet, check the actual ACMD and use that instead
		if( (code== "CMD55") && (i+2 < m_packets.size()) )
		{
			Packet* next = m_packets[i+2];

			ret->m_headers["Command"] = next->m_headers["Command"];
			ret->m_headers["Code"] = next->m_headers["Code"];
			ret->m_displayBackgroundColor = next->m_displayBackgroundColor;
			ret->m_headers["Info"] = next->m_headers["Info"];

			//Summarize ACMD41 with reply data
			if(next->m_headers["Code"] == "ACMD41")
			{
				//Keep on looking at replies until we see the final ACMD41
				size_t last = i+2;
				for(size_t j=i; j<m_packets.size(); j++)
				{
					if(m_packets[j]->m_headers["Type"] != "Reply")
						continue;
					else if(m_packets[j]->m_headers["Code"] == "CMD55")
						continue;
					else if(m_packets[j]->m_headers["Code"] == "ACMD41")
						last = j;
					else
						break;
				}
				ret->m_headers["Info"] += string(", got ") + m_packets[last]->m_headers["Info"];
			}
		}

		//Summarize CMD2, and CMD3 with reply data
		if( (code == "CMD2") && (i+1 < m_packets.size()) )
			ret->m_headers["Info"] = m_packets[i+1]->m_headers["Info"];
		if( (code== "CMD3") && (i+1 < m_packets.size()) )
			ret->m_headers["Info"] = m_packets[i+1]->m_headers["Info"];

		//For CMD1 and CMD13, use last reply
		if( (code == "CMD1") || (code == "CMD13") )
		{
			for(; i < m_packets.size(); i++)
			{
				if(m_packets[i]->m_headers["Code"] != code)
					break;
				if(m_packets[i]->m_headers["Type"] != "Reply")
					continue;
				ret->m_headers["Info"] = m_packets[i]->m_headers["Info"];
			}
		}

		return ret;
	}

	return NULL;
}
