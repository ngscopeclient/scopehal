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

#include "../scopehal/scopehal.h"
#include "PRBSGeneratorFilter.h"

using namespace std;

//LFSR lookahead table for PRBS-23 polynomial
const uint32_t g_prbs23Table[23][23] =
{
	{ 0x000002, 0x000004, 0x000008, 0x000010, 0x000020, 0x000040, 0x000080, 0x000100, 0x000200, 0x000400, 0x000800, 0x001000, 0x002000, 0x004000, 0x008000, 0x010000, 0x020000, 0x040001, 0x080000, 0x100000, 0x200000, 0x400000, 0x000001 },	//0
	{ 0x000004, 0x000008, 0x000010, 0x000020, 0x000040, 0x000080, 0x000100, 0x000200, 0x000400, 0x000800, 0x001000, 0x002000, 0x004000, 0x008000, 0x010000, 0x020000, 0x040001, 0x080002, 0x100000, 0x200000, 0x400000, 0x000001, 0x000002 },	//1
	{ 0x000010, 0x000020, 0x000040, 0x000080, 0x000100, 0x000200, 0x000400, 0x000800, 0x001000, 0x002000, 0x004000, 0x008000, 0x010000, 0x020000, 0x040001, 0x080002, 0x100004, 0x200008, 0x400000, 0x000001, 0x000002, 0x000004, 0x000008 },	//2
	{ 0x000100, 0x000200, 0x000400, 0x000800, 0x001000, 0x002000, 0x004000, 0x008000, 0x010000, 0x020000, 0x040001, 0x080002, 0x100004, 0x200008, 0x400010, 0x000021, 0x000042, 0x000084, 0x000008, 0x000010, 0x000020, 0x000040, 0x000080 },	//3
	{ 0x010000, 0x020000, 0x040001, 0x080002, 0x100004, 0x200008, 0x400010, 0x000021, 0x000042, 0x000084, 0x000108, 0x000210, 0x000420, 0x000840, 0x001080, 0x002100, 0x004200, 0x008400, 0x000800, 0x001000, 0x002000, 0x004000, 0x008000 },	//4
	{ 0x004200, 0x008400, 0x010800, 0x021000, 0x042001, 0x084002, 0x108004, 0x210008, 0x420010, 0x040020, 0x080040, 0x100080, 0x200100, 0x400200, 0x000401, 0x000802, 0x001004, 0x002008, 0x000210, 0x000420, 0x000840, 0x001080, 0x002100 },	//5
	{ 0x040421, 0x080842, 0x101084, 0x202108, 0x404210, 0x008421, 0x010842, 0x021084, 0x042109, 0x084212, 0x108424, 0x210848, 0x421090, 0x042120, 0x084240, 0x108480, 0x210900, 0x421200, 0x002021, 0x004042, 0x008084, 0x010108, 0x020210 },	//6
	{ 0x142405, 0x28480a, 0x509014, 0x212029, 0x424052, 0x0480a4, 0x090148, 0x120290, 0x240521, 0x480a42, 0x101485, 0x20290a, 0x405214, 0x00a429, 0x014852, 0x0290a4, 0x052149, 0x0a4292, 0x00a120, 0x014240, 0x028480, 0x050901, 0x0a1202 },	//7
	{ 0x56211d, 0x2c423a, 0x588474, 0x3108e9, 0x6211d2, 0x4423a4, 0x084749, 0x108e92, 0x211d24, 0x423a48, 0x047490, 0x08e920, 0x11d240, 0x23a480, 0x474901, 0x0e9202, 0x1d2405, 0x3a480a, 0x22b108, 0x456211, 0x0ac423, 0x158847, 0x2b108e },	//8
	{ 0x662859, 0x4c50b2, 0x18a165, 0x3142ca, 0x628594, 0x450b28, 0x0a1651, 0x142ca3, 0x285946, 0x50b28c, 0x216519, 0x42ca32, 0x059464, 0x0b28c8, 0x165191, 0x2ca323, 0x594646, 0x328c8d, 0x033142, 0x066285, 0x0cc50b, 0x198a16, 0x33142c },	//9
	{ 0x6d3859, 0x5a70b3, 0x34e166, 0x69c2cc, 0x538599, 0x270b32, 0x4e1665, 0x1c2cca, 0x385994, 0x70b328, 0x616651, 0x42cca3, 0x059946, 0x0b328c, 0x166519, 0x2cca33, 0x599466, 0x3328cd, 0x0b69c2, 0x16d385, 0x2da70b, 0x5b4e16, 0x369c2c },	//10
	{ 0x7cf21b, 0x79e437, 0x73c86f, 0x6790de, 0x4f21bc, 0x1e4378, 0x3c86f1, 0x790de2, 0x721bc5, 0x64378a, 0x486f15, 0x10de2b, 0x21bc56, 0x4378ac, 0x06f158, 0x0de2b1, 0x1bc562, 0x378ac5, 0x13e790, 0x27cf21, 0x4f9e43, 0x1f3c86, 0x3e790d },	//11
	{ 0x7ab4ae, 0x75695c, 0x6ad2b9, 0x55a572, 0x2b4ae5, 0x5695cb, 0x2d2b96, 0x5a572c, 0x34ae58, 0x695cb0, 0x52b961, 0x2572c2, 0x4ae584, 0x15cb08, 0x2b9610, 0x572c21, 0x2e5842, 0x5cb085, 0x43d5a5, 0x07ab4a, 0x0f5695, 0x1ead2b, 0x3d5a57 },	//12
	{ 0x6bdd9a, 0x57bb34, 0x2f7668, 0x5eecd1, 0x3dd9a2, 0x7bb344, 0x776688, 0x6ecd10, 0x5d9a20, 0x3b3441, 0x766883, 0x6cd106, 0x59a20d, 0x33441b, 0x668837, 0x4d106e, 0x1a20dd, 0x3441bb, 0x035eec, 0x06bdd9, 0x0d7bb3, 0x1af766, 0x35eecd },	//13
	{ 0x689fb2, 0x513f65, 0x227ecb, 0x44fd97, 0x09fb2f, 0x13f65e, 0x27ecbd, 0x4fd97b, 0x1fb2f6, 0x3f65ed, 0x7ecbdb, 0x7d97b6, 0x7b2f6d, 0x765eda, 0x6cbdb4, 0x597b69, 0x32f6d3, 0x65eda7, 0x2344fd, 0x4689fb, 0x0d13f6, 0x1a27ec, 0x344fd9 },	//14
	{ 0x6dd5d3, 0x5baba7, 0x37574e, 0x6eae9d, 0x5d5d3a, 0x3aba75, 0x7574eb, 0x6ae9d7, 0x55d3ae, 0x2ba75d, 0x574ebb, 0x2e9d76, 0x5d3aed, 0x3a75db, 0x74ebb7, 0x69d76f, 0x53aedf, 0x275dbe, 0x236eae, 0x46dd5d, 0x0dbaba, 0x1b7574, 0x36eae9 },	//15
	{ 0x2da7e3, 0x5b4fc6, 0x369f8c, 0x6d3f19, 0x5a7e33, 0x34fc66, 0x69f8cc, 0x53f199, 0x27e332, 0x4fc665, 0x1f8cca, 0x3f1995, 0x7e332b, 0x7c6656, 0x78ccad, 0x71995b, 0x6332b7, 0x46656e, 0x216d3f, 0x42da7e, 0x05b4fc, 0x0b69f8, 0x16d3f1 },	//16
	{ 0x09a788, 0x134f10, 0x269e21, 0x4d3c43, 0x1a7887, 0x34f10f, 0x69e21e, 0x53c43d, 0x27887a, 0x4f10f5, 0x1e21ea, 0x3c43d5, 0x7887aa, 0x710f55, 0x621eab, 0x443d56, 0x087aad, 0x10f55a, 0x284d3c, 0x509a78, 0x2134f1, 0x4269e2, 0x04d3c4 },	//17
	{ 0x0593cd, 0x0b279a, 0x164f35, 0x2c9e6b, 0x593cd6, 0x3279ad, 0x64f35b, 0x49e6b7, 0x13cd6f, 0x279adf, 0x4f35bf, 0x1e6b7e, 0x3cd6fd, 0x79adfa, 0x735bf5, 0x66b7ea, 0x4d6fd4, 0x1adfa9, 0x302c9e, 0x60593c, 0x40b279, 0x0164f3, 0x02c9e6 },	//18
	{ 0x012292, 0x024524, 0x048a49, 0x091492, 0x122924, 0x245249, 0x48a492, 0x114925, 0x22924a, 0x452495, 0x0a492b, 0x149257, 0x2924ae, 0x52495c, 0x2492b8, 0x492570, 0x124ae1, 0x2495c3, 0x480914, 0x101229, 0x202452, 0x4048a4, 0x009149 },	//19
	{ 0x04020d, 0x08041a, 0x100834, 0x201068, 0x4020d0, 0x0041a1, 0x008342, 0x010684, 0x020d08, 0x041a11, 0x083422, 0x106844, 0x20d088, 0x41a110, 0x034221, 0x068443, 0x0d0887, 0x1a110e, 0x302010, 0x604020, 0x408041, 0x010083, 0x020106 },	//20
	{ 0x002050, 0x0040a0, 0x008140, 0x010280, 0x020500, 0x040a01, 0x081402, 0x102804, 0x205008, 0x40a010, 0x014021, 0x028042, 0x050085, 0x0a010a, 0x140215, 0x28042a, 0x500854, 0x2010a9, 0x400102, 0x000205, 0x00040a, 0x000814, 0x001028 },	//21
	{ 0x001008, 0x002010, 0x004020, 0x008040, 0x010080, 0x020100, 0x040201, 0x080402, 0x100804, 0x201008, 0x402010, 0x004021, 0x008042, 0x010084, 0x020108, 0x040211, 0x080422, 0x100844, 0x200080, 0x400100, 0x000201, 0x000402, 0x000804 },	//22
};

//LFSR lookahead table for PRBS-31 polynomial
const uint32_t g_prbs31Table[30][31] =
{
	{ 0x00000002, 0x00000004, 0x00000008, 0x00000010, 0x00000020, 0x00000040, 0x00000080, 0x00000100, 0x00000200, 0x00000400, 0x00000800, 0x00001000, 0x00002000, 0x00004000, 0x00008000, 0x00010000, 0x00020000, 0x00040000, 0x00080000, 0x00100000, 0x00200000, 0x00400000, 0x00800000, 0x01000000, 0x02000000, 0x04000000, 0x08000000, 0x10000001, 0x20000000, 0x40000000, 0x00000001 },	//0
	{ 0x00000004, 0x00000008, 0x00000010, 0x00000020, 0x00000040, 0x00000080, 0x00000100, 0x00000200, 0x00000400, 0x00000800, 0x00001000, 0x00002000, 0x00004000, 0x00008000, 0x00010000, 0x00020000, 0x00040000, 0x00080000, 0x00100000, 0x00200000, 0x00400000, 0x00800000, 0x01000000, 0x02000000, 0x04000000, 0x08000000, 0x10000001, 0x20000002, 0x40000000, 0x00000001, 0x00000002 },	//1
	{ 0x00000010, 0x00000020, 0x00000040, 0x00000080, 0x00000100, 0x00000200, 0x00000400, 0x00000800, 0x00001000, 0x00002000, 0x00004000, 0x00008000, 0x00010000, 0x00020000, 0x00040000, 0x00080000, 0x00100000, 0x00200000, 0x00400000, 0x00800000, 0x01000000, 0x02000000, 0x04000000, 0x08000000, 0x10000001, 0x20000002, 0x40000004, 0x00000009, 0x00000002, 0x00000004, 0x00000008 },	//2
	{ 0x00000100, 0x00000200, 0x00000400, 0x00000800, 0x00001000, 0x00002000, 0x00004000, 0x00008000, 0x00010000, 0x00020000, 0x00040000, 0x00080000, 0x00100000, 0x00200000, 0x00400000, 0x00800000, 0x01000000, 0x02000000, 0x04000000, 0x08000000, 0x10000001, 0x20000002, 0x40000004, 0x00000009, 0x00000012, 0x00000024, 0x00000048, 0x00000090, 0x00000020, 0x00000040, 0x00000080 },	//3
	{ 0x00010000, 0x00020000, 0x00040000, 0x00080000, 0x00100000, 0x00200000, 0x00400000, 0x00800000, 0x01000000, 0x02000000, 0x04000000, 0x08000000, 0x10000001, 0x20000002, 0x40000004, 0x00000009, 0x00000012, 0x00000024, 0x00000048, 0x00000090, 0x00000120, 0x00000240, 0x00000480, 0x00000900, 0x00001200, 0x00002400, 0x00004800, 0x00009000, 0x00002000, 0x00004000, 0x00008000 },	//4
	{ 0x00000012, 0x00000024, 0x00000048, 0x00000090, 0x00000120, 0x00000240, 0x00000480, 0x00000900, 0x00001200, 0x00002400, 0x00004800, 0x00009000, 0x00012000, 0x00024000, 0x00048000, 0x00090000, 0x00120000, 0x00240000, 0x00480000, 0x00900000, 0x01200000, 0x02400000, 0x04800000, 0x09000000, 0x12000001, 0x24000002, 0x48000004, 0x10000008, 0x20000002, 0x40000004, 0x00000009 },	//5
	{ 0x00000104, 0x00000208, 0x00000410, 0x00000820, 0x00001040, 0x00002080, 0x00004100, 0x00008200, 0x00010400, 0x00020800, 0x00041000, 0x00082000, 0x00104000, 0x00208000, 0x00410000, 0x00820000, 0x01040000, 0x02080000, 0x04100000, 0x08200000, 0x10400001, 0x20800002, 0x41000004, 0x02000009, 0x04000012, 0x08000024, 0x10000049, 0x20000092, 0x40000020, 0x00000041, 0x00000082 },	//6
	{ 0x00010010, 0x00020020, 0x00040040, 0x00080080, 0x00100100, 0x00200200, 0x00400400, 0x00800800, 0x01001000, 0x02002000, 0x04004000, 0x08008000, 0x10010001, 0x20020002, 0x40040004, 0x00080009, 0x00100012, 0x00200024, 0x00400048, 0x00800090, 0x01000120, 0x02000240, 0x04000480, 0x08000900, 0x10001201, 0x20002402, 0x40004804, 0x00009009, 0x00002002, 0x00004004, 0x00008008 },	//7
	{ 0x00000112, 0x00000224, 0x00000448, 0x00000890, 0x00001120, 0x00002240, 0x00004480, 0x00008900, 0x00011200, 0x00022400, 0x00044800, 0x00089000, 0x00112000, 0x00224000, 0x00448000, 0x00890000, 0x01120000, 0x02240000, 0x04480000, 0x08900000, 0x11200001, 0x22400002, 0x44800004, 0x09000009, 0x12000013, 0x24000026, 0x4800004c, 0x10000098, 0x20000022, 0x40000044, 0x00000089 },	//8
	{ 0x00010104, 0x00020208, 0x00040410, 0x00080820, 0x00101040, 0x00202080, 0x00404100, 0x00808200, 0x01010400, 0x02020800, 0x04041000, 0x08082000, 0x10104001, 0x20208002, 0x40410004, 0x00820009, 0x01040012, 0x02080024, 0x04100048, 0x08200090, 0x10400121, 0x20800242, 0x41000484, 0x02000909, 0x04001212, 0x08002424, 0x10004849, 0x20009092, 0x40002020, 0x00004041, 0x00008082 },	//9
	{ 0x00010002, 0x00020004, 0x00040008, 0x00080010, 0x00100020, 0x00200040, 0x00400080, 0x00800100, 0x01000200, 0x02000400, 0x04000800, 0x08001000, 0x10002001, 0x20004002, 0x40008004, 0x00010009, 0x00020012, 0x00040024, 0x00080048, 0x00100090, 0x00200120, 0x00400240, 0x00800480, 0x01000900, 0x02001200, 0x04002400, 0x08004800, 0x10009001, 0x20002000, 0x40004000, 0x00008001 },	//10
	{ 0x00000016, 0x0000002c, 0x00000058, 0x000000b0, 0x00000160, 0x000002c0, 0x00000580, 0x00000b00, 0x00001600, 0x00002c00, 0x00005800, 0x0000b000, 0x00016000, 0x0002c000, 0x00058000, 0x000b0000, 0x00160000, 0x002c0000, 0x00580000, 0x00b00000, 0x01600000, 0x02c00000, 0x05800000, 0x0b000000, 0x16000001, 0x2c000002, 0x58000005, 0x3000000a, 0x60000002, 0x40000005, 0x0000000b },	//11
	{ 0x00000114, 0x00000228, 0x00000450, 0x000008a0, 0x00001140, 0x00002280, 0x00004500, 0x00008a00, 0x00011400, 0x00022800, 0x00045000, 0x0008a000, 0x00114000, 0x00228000, 0x00450000, 0x008a0000, 0x01140000, 0x02280000, 0x04500000, 0x08a00000, 0x11400001, 0x22800002, 0x45000004, 0x0a000009, 0x14000013, 0x28000026, 0x5000004d, 0x2000009b, 0x40000022, 0x00000045, 0x0000008a },	//12
	{ 0x00010110, 0x00020220, 0x00040440, 0x00080880, 0x00101100, 0x00202200, 0x00404400, 0x00808800, 0x01011000, 0x02022000, 0x04044000, 0x08088000, 0x10110001, 0x20220002, 0x40440004, 0x00880009, 0x01100012, 0x02200024, 0x04400048, 0x08800090, 0x11000121, 0x22000242, 0x44000484, 0x08000909, 0x10001213, 0x20002426, 0x4000484c, 0x00009099, 0x00002022, 0x00004044, 0x00008088 },	//13
	{ 0x00010112, 0x00020224, 0x00040448, 0x00080890, 0x00101120, 0x00202240, 0x00404480, 0x00808900, 0x01011200, 0x02022400, 0x04044800, 0x08089000, 0x10112001, 0x20224002, 0x40448004, 0x00890009, 0x01120012, 0x02240024, 0x04480048, 0x08900090, 0x11200121, 0x22400242, 0x44800484, 0x09000909, 0x12001213, 0x24002426, 0x4800484c, 0x10009098, 0x20002022, 0x40004044, 0x00008089 },	//14
	{ 0x00010116, 0x0002022c, 0x00040458, 0x000808b0, 0x00101160, 0x002022c0, 0x00404580, 0x00808b00, 0x01011600, 0x02022c00, 0x04045800, 0x0808b000, 0x10116001, 0x2022c002, 0x40458004, 0x008b0009, 0x01160012, 0x022c0024, 0x04580048, 0x08b00090, 0x11600121, 0x22c00242, 0x45800484, 0x0b000909, 0x16001213, 0x2c002426, 0x5800484d, 0x3000909a, 0x60002022, 0x40004045, 0x0000808b },	//15
	{ 0x00010106, 0x0002020c, 0x00040418, 0x00080830, 0x00101060, 0x002020c0, 0x00404180, 0x00808300, 0x01010600, 0x02020c00, 0x04041800, 0x08083000, 0x10106001, 0x2020c002, 0x40418004, 0x00830009, 0x01060012, 0x020c0024, 0x04180048, 0x08300090, 0x10600121, 0x20c00242, 0x41800484, 0x03000909, 0x06001212, 0x0c002424, 0x18004849, 0x30009093, 0x60002020, 0x40004041, 0x00008083 },	//16
	{ 0x00010006, 0x0002000c, 0x00040018, 0x00080030, 0x00100060, 0x002000c0, 0x00400180, 0x00800300, 0x01000600, 0x02000c00, 0x04001800, 0x08003000, 0x10006001, 0x2000c002, 0x40018004, 0x00030009, 0x00060012, 0x000c0024, 0x00180048, 0x00300090, 0x00600120, 0x00c00240, 0x01800480, 0x03000900, 0x06001200, 0x0c002400, 0x18004801, 0x30009003, 0x60002000, 0x40004001, 0x00008003 },	//17
	{ 0x00000006, 0x0000000c, 0x00000018, 0x00000030, 0x00000060, 0x000000c0, 0x00000180, 0x00000300, 0x00000600, 0x00000c00, 0x00001800, 0x00003000, 0x00006000, 0x0000c000, 0x00018000, 0x00030000, 0x00060000, 0x000c0000, 0x00180000, 0x00300000, 0x00600000, 0x00c00000, 0x01800000, 0x03000000, 0x06000000, 0x0c000000, 0x18000001, 0x30000003, 0x60000000, 0x40000001, 0x00000003 },	//18
	{ 0x00000014, 0x00000028, 0x00000050, 0x000000a0, 0x00000140, 0x00000280, 0x00000500, 0x00000a00, 0x00001400, 0x00002800, 0x00005000, 0x0000a000, 0x00014000, 0x00028000, 0x00050000, 0x000a0000, 0x00140000, 0x00280000, 0x00500000, 0x00a00000, 0x01400000, 0x02800000, 0x05000000, 0x0a000000, 0x14000001, 0x28000002, 0x50000005, 0x2000000b, 0x40000002, 0x00000005, 0x0000000a },	//19
	{ 0x00000110, 0x00000220, 0x00000440, 0x00000880, 0x00001100, 0x00002200, 0x00004400, 0x00008800, 0x00011000, 0x00022000, 0x00044000, 0x00088000, 0x00110000, 0x00220000, 0x00440000, 0x00880000, 0x01100000, 0x02200000, 0x04400000, 0x08800000, 0x11000001, 0x22000002, 0x44000004, 0x08000009, 0x10000013, 0x20000026, 0x4000004c, 0x00000099, 0x00000022, 0x00000044, 0x00000088 },	//20
	{ 0x00010100, 0x00020200, 0x00040400, 0x00080800, 0x00101000, 0x00202000, 0x00404000, 0x00808000, 0x01010000, 0x02020000, 0x04040000, 0x08080000, 0x10100001, 0x20200002, 0x40400004, 0x00800009, 0x01000012, 0x02000024, 0x04000048, 0x08000090, 0x10000121, 0x20000242, 0x40000484, 0x00000909, 0x00001212, 0x00002424, 0x00004848, 0x00009090, 0x00002020, 0x00004040, 0x00008080 },	//21
	{ 0x00010012, 0x00020024, 0x00040048, 0x00080090, 0x00100120, 0x00200240, 0x00400480, 0x00800900, 0x01001200, 0x02002400, 0x04004800, 0x08009000, 0x10012001, 0x20024002, 0x40048004, 0x00090009, 0x00120012, 0x00240024, 0x00480048, 0x00900090, 0x01200120, 0x02400240, 0x04800480, 0x09000900, 0x12001201, 0x24002402, 0x48004804, 0x10009008, 0x20002002, 0x40004004, 0x00008009 },	//22
	{ 0x00000116, 0x0000022c, 0x00000458, 0x000008b0, 0x00001160, 0x000022c0, 0x00004580, 0x00008b00, 0x00011600, 0x00022c00, 0x00045800, 0x0008b000, 0x00116000, 0x0022c000, 0x00458000, 0x008b0000, 0x01160000, 0x022c0000, 0x04580000, 0x08b00000, 0x11600001, 0x22c00002, 0x45800004, 0x0b000009, 0x16000013, 0x2c000026, 0x5800004d, 0x3000009a, 0x60000022, 0x40000045, 0x0000008b },	//23
	{ 0x00010114, 0x00020228, 0x00040450, 0x000808a0, 0x00101140, 0x00202280, 0x00404500, 0x00808a00, 0x01011400, 0x02022800, 0x04045000, 0x0808a000, 0x10114001, 0x20228002, 0x40450004, 0x008a0009, 0x01140012, 0x02280024, 0x04500048, 0x08a00090, 0x11400121, 0x22800242, 0x45000484, 0x0a000909, 0x14001213, 0x28002426, 0x5000484d, 0x2000909b, 0x40002022, 0x00004045, 0x0000808a },	//24
	{ 0x00010102, 0x00020204, 0x00040408, 0x00080810, 0x00101020, 0x00202040, 0x00404080, 0x00808100, 0x01010200, 0x02020400, 0x04040800, 0x08081000, 0x10102001, 0x20204002, 0x40408004, 0x00810009, 0x01020012, 0x02040024, 0x04080048, 0x08100090, 0x10200121, 0x20400242, 0x40800484, 0x01000909, 0x02001212, 0x04002424, 0x08004848, 0x10009091, 0x20002020, 0x40004040, 0x00008081 },	//25
	{ 0x00010016, 0x0002002c, 0x00040058, 0x000800b0, 0x00100160, 0x002002c0, 0x00400580, 0x00800b00, 0x01001600, 0x02002c00, 0x04005800, 0x0800b000, 0x10016001, 0x2002c002, 0x40058004, 0x000b0009, 0x00160012, 0x002c0024, 0x00580048, 0x00b00090, 0x01600120, 0x02c00240, 0x05800480, 0x0b000900, 0x16001201, 0x2c002402, 0x58004805, 0x3000900a, 0x60002002, 0x40004005, 0x0000800b },	//26
	{ 0x00000106, 0x0000020c, 0x00000418, 0x00000830, 0x00001060, 0x000020c0, 0x00004180, 0x00008300, 0x00010600, 0x00020c00, 0x00041800, 0x00083000, 0x00106000, 0x0020c000, 0x00418000, 0x00830000, 0x01060000, 0x020c0000, 0x04180000, 0x08300000, 0x10600001, 0x20c00002, 0x41800004, 0x03000009, 0x06000012, 0x0c000024, 0x18000049, 0x30000093, 0x60000020, 0x40000041, 0x00000083 },	//27
	{ 0x00010014, 0x00020028, 0x00040050, 0x000800a0, 0x00100140, 0x00200280, 0x00400500, 0x00800a00, 0x01001400, 0x02002800, 0x04005000, 0x0800a000, 0x10014001, 0x20028002, 0x40050004, 0x000a0009, 0x00140012, 0x00280024, 0x00500048, 0x00a00090, 0x01400120, 0x02800240, 0x05000480, 0x0a000900, 0x14001201, 0x28002402, 0x50004805, 0x2000900b, 0x40002002, 0x00004005, 0x0000800a },	//28
	{ 0x00000102, 0x00000204, 0x00000408, 0x00000810, 0x00001020, 0x00002040, 0x00004080, 0x00008100, 0x00010200, 0x00020400, 0x00040800, 0x00081000, 0x00102000, 0x00204000, 0x00408000, 0x00810000, 0x01020000, 0x02040000, 0x04080000, 0x08100000, 0x10200001, 0x20400002, 0x40800004, 0x01000009, 0x02000012, 0x04000024, 0x08000048, 0x10000091, 0x20000020, 0x40000040, 0x00000081 }	//29
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

PRBSGeneratorFilter::PRBSGeneratorFilter(const string& color)
	: Filter(color, CAT_GENERATION)
	, m_baud(m_parameters["Data Rate"])
	, m_poly(m_parameters["Polynomial"])
	, m_depth(m_parameters["Depth"])
	, m_prbs23Table("PRBSGeneratorFilter.m_prbs23Table")
	, m_prbs31Table("PRBSGeneratorFilter.m_prbs31Table")
{
	AddStream(Unit(Unit::UNIT_COUNTS), "Data", Stream::STREAM_TYPE_DIGITAL);
	AddStream(Unit(Unit::UNIT_COUNTS), "Clock", Stream::STREAM_TYPE_DIGITAL);

	m_baud = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_BITRATE));
	m_baud.SetIntVal(103125LL * 100LL * 1000LL);

	m_poly = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_poly.AddEnumValue("PRBS-7", POLY_PRBS7);
	m_poly.AddEnumValue("PRBS-9", POLY_PRBS9);
	m_poly.AddEnumValue("PRBS-11", POLY_PRBS11);
	m_poly.AddEnumValue("PRBS-15", POLY_PRBS15);
	m_poly.AddEnumValue("PRBS-23", POLY_PRBS23);
	m_poly.AddEnumValue("PRBS-31", POLY_PRBS31);
	m_poly.SetIntVal(POLY_PRBS7);

	m_depth = FilterParameter(FilterParameter::TYPE_INT, Unit(Unit::UNIT_SAMPLEDEPTH));
	m_depth.SetIntVal(100 * 1000);

	if(g_hasShaderInt8)
	{
		m_prbs7Pipeline = make_shared<ComputePipeline>(
			"shaders/PRBS7.spv",
			1,
			sizeof(PRBSGeneratorConstants));

		m_prbs9Pipeline = make_shared<ComputePipeline>(
			"shaders/PRBS9.spv",
			1,
			sizeof(PRBSGeneratorConstants));

		m_prbs11Pipeline = make_shared<ComputePipeline>(
			"shaders/PRBS11.spv",
			1,
			sizeof(PRBSGeneratorConstants));

		m_prbs15Pipeline = make_shared<ComputePipeline>(
			"shaders/PRBS15.spv",
			1,
			sizeof(PRBSGeneratorConstants));

		//PRBS-23 and up need table for lookahead since they don't run an entire LFSR cycle per thread
		m_prbs23Pipeline = make_shared<ComputePipeline>(
			"shaders/PRBS23.spv",
			2,
			sizeof(PRBSGeneratorBlockConstants));
		m_prbs31Pipeline = make_shared<ComputePipeline>(
			"shaders/PRBS31.spv",
			2,
			sizeof(PRBSGeneratorBlockConstants));

		//Fill lookahead table for PRBS-23
		uint32_t rows = 23;
		uint32_t cols = rows;
		m_prbs23Table.resize(rows * cols);
		m_prbs23Table.PrepareForCpuAccess();
		m_prbs23Table.SetGpuAccessHint(AcceleratorBuffer<uint32_t>::HINT_LIKELY);
		for(uint32_t row=0; row<rows; row++)
		{
			for(uint32_t col=0; col<cols; col++)
				m_prbs23Table[row*cols + col] = g_prbs23Table[row][col];
		}
		m_prbs23Table.MarkModifiedFromCpu();

		//Fill lookahead table for PRBS-31
		//(last row omitted because our max waveform size is only 2^30)
		rows = 30;
		cols = 31;
		m_prbs31Table.resize(rows * cols);
		m_prbs31Table.PrepareForCpuAccess();
		m_prbs31Table.SetGpuAccessHint(AcceleratorBuffer<uint32_t>::HINT_LIKELY);
		for(uint32_t row=0; row<rows; row++)
		{
			for(uint32_t col=0; col<cols; col++)
				m_prbs31Table[row*cols + col] = g_prbs31Table[row][col];
		}
		m_prbs31Table.MarkModifiedFromCpu();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool PRBSGeneratorFilter::ValidateChannel(size_t /*i*/, StreamDescriptor /*stream*/)
{
	//no inputs
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string PRBSGeneratorFilter::GetProtocolName()
{
	return "PRBS";
}

void PRBSGeneratorFilter::SetDefaultName()
{
	Unit rate(Unit::UNIT_BITRATE);

	string prefix = "";
	switch(m_poly.GetIntVal())
	{
		case POLY_PRBS7:
			prefix = "PRBS7";
			break;

		case POLY_PRBS9:
			prefix = "PRBS9";
			break;

		case POLY_PRBS11:
			prefix = "PRBS11";
			break;

		case POLY_PRBS15:
			prefix = "PRBS15";
			break;

		case POLY_PRBS23:
			prefix = "PRBS23";
			break;

		case POLY_PRBS31:
		default:
			prefix = "PRBS31";
			break;
	}

	m_hwname = prefix + "(" + rate.PrettyPrint(m_baud.GetIntVal()).c_str() + ")";
	m_displayname = m_hwname;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

bool PRBSGeneratorFilter::RunPRBS(uint32_t& state, Polynomials poly)
{
	uint32_t next;
	switch(poly)
	{
		case POLY_PRBS7:
			next = ( (state >> 6) ^ (state >> 5) ) & 1;
			break;

		case POLY_PRBS9:
			next = ( (state >> 8) ^ (state >> 4) ) & 1;
			break;

		case POLY_PRBS11:
			next = ( (state >> 10) ^ (state >> 8) ) & 1;
			break;

		case POLY_PRBS15:
			next = ( (state >> 14) ^ (state >> 13) ) & 1;
			break;

		case POLY_PRBS23:
			next = ( (state >> 22) ^ (state >> 17) ) & 1;
			break;

		case POLY_PRBS31:
		default:
			next = ( (state >> 30) ^ (state >> 27) ) & 1;
			break;
	}
	state = (state << 1) | next;
	return (bool)next;
}

Filter::DataLocation PRBSGeneratorFilter::GetInputLocation()
{
	//We explicitly manage our input memory and don't care where it is when Refresh() is called
	return LOC_DONTCARE;
}

void PRBSGeneratorFilter::Refresh(
	[[maybe_unused]] vk::raii::CommandBuffer& cmdBuf,
	[[maybe_unused]] shared_ptr<QueueHandle> queue)
{
	#ifdef HAVE_NVTX
		nvtx3::scoped_range nrange("PRBSGeneratorFilter::Refresh");
	#endif

	size_t depth = m_depth.GetIntVal();
	int64_t baudrate = m_baud.GetIntVal();
	auto poly = static_cast<Polynomials>(m_poly.GetIntVal());
	size_t samplePeriod = FS_PER_SECOND / baudrate;

	double t = GetTime();
	int64_t fs = (t - floor(t)) * FS_PER_SECOND;

	//Create the two output waveforms
	auto dat = dynamic_cast<UniformDigitalWaveform*>(GetData(0));
	if(!dat)
	{
		dat = new UniformDigitalWaveform;
		SetData(dat, 0);
	}
	dat->m_timescale = samplePeriod;
	dat->m_triggerPhase = 0;
	dat->m_startTimestamp = floor(t);
	dat->m_startFemtoseconds = fs;
	dat->Resize(depth);

	//Set up the clock waveform
	auto clk = dynamic_cast<UniformDigitalWaveform*>(GetData(1));
	if(!clk)
	{
		clk = new UniformDigitalWaveform;
		SetData(clk, 1);
	}
	clk->m_timescale = samplePeriod;
	clk->m_triggerPhase = samplePeriod / 2;
	clk->m_startTimestamp = floor(t);
	clk->m_startFemtoseconds = fs;
	size_t oldClockSize = clk->size();
	clk->Resize(depth);

	//Only generate the clock waveform if we changed length
	if(oldClockSize != depth)
	{
		clk->PrepareForCpuAccess();

		bool lastclk = false;
		for(size_t i=0; i<depth; i++)
		{
			clk->m_samples[i] = lastclk;
			lastclk = !lastclk;
		}

		clk->MarkModifiedFromCpu();
	}

	//GPU path
	if(g_hasShaderInt8)
	{
		//Config for short sequences
		PRBSGeneratorConstants cfg;
		cfg.count = depth;
		cfg.seed = rand();

		//Config for long sequences
		uint32_t numBlockThreads = 131072;
		PRBSGeneratorBlockConstants blockcfg;
		blockcfg.count = depth;
		blockcfg.seed = rand();
		blockcfg.samplesPerThread = GetComputeBlockCount(depth, numBlockThreads);

		//Figure out the shader and thread block count to use
		uint32_t numThreads = 0;
		shared_ptr<ComputePipeline> pipe;
		switch(poly)
		{
			case POLY_PRBS7:
				numThreads = GetComputeBlockCount(depth, 127);
				pipe = m_prbs7Pipeline;
				break;

			case POLY_PRBS9:
				numThreads = GetComputeBlockCount(depth, 511);
				pipe = m_prbs9Pipeline;
				break;

			case POLY_PRBS11:
				numThreads = GetComputeBlockCount(depth, 2047);
				pipe = m_prbs11Pipeline;
				break;

			case POLY_PRBS15:
				numThreads = GetComputeBlockCount(depth, 32767);
				pipe = m_prbs15Pipeline;
				break;

			case POLY_PRBS23:
				numThreads = numBlockThreads;
				pipe = m_prbs23Pipeline;
				break;

			case POLY_PRBS31:
				numThreads = numBlockThreads;
				pipe = m_prbs31Pipeline;
				break;

			default:
				break;
		}
		const uint32_t threadsPerBlock = 64;
		const uint32_t compute_block_count = GetComputeBlockCount(numThreads, threadsPerBlock);

		switch(poly)
		{
			//Shorter sequences: Each thread generates a full PRBS cycle from the chosen offset
			case POLY_PRBS7:
			case POLY_PRBS9:
			case POLY_PRBS11:
			case POLY_PRBS15:
				{
					cmdBuf.begin({});

					pipe->BindBufferNonblocking(0, dat->m_samples, cmdBuf, true);
					pipe->Dispatch(cmdBuf, cfg,
						min(compute_block_count, 32768u),
						compute_block_count / 32768 + 1);

					cmdBuf.end();
					queue->SubmitAndBlock(cmdBuf);

					dat->m_samples.MarkModifiedFromGpu();
				}
				break;

			//Larger sequences have separate structure with lookahead
			case POLY_PRBS23:
			case POLY_PRBS31:
				{
					cmdBuf.begin({});

					pipe->BindBufferNonblocking(0, dat->m_samples, cmdBuf, true);

					if(poly == POLY_PRBS23)
						pipe->BindBufferNonblocking(1, m_prbs23Table, cmdBuf);
					else
						pipe->BindBufferNonblocking(1, m_prbs31Table, cmdBuf);

					pipe->Dispatch(cmdBuf, blockcfg,
						min(compute_block_count, 32768u),
						compute_block_count / 32768 + 1);

					cmdBuf.end();
					queue->SubmitAndBlock(cmdBuf);

					dat->m_samples.MarkModifiedFromGpu();
				}
				break;

			//Software fallback
			default:
				{
					//Always generate the PRBS
					dat->m_samples.PrepareForCpuAccess();

					uint32_t prbs = cfg.seed;
					for(size_t i=0; i<depth; i++)
						dat->m_samples[i] = RunPRBS(prbs, poly);

					dat->m_samples.MarkModifiedFromCpu();
				}
				break;
		}
	}

	//CPU fallback
	else
	{
		//Always generate the PRBS CPU side
		dat->m_samples.PrepareForCpuAccess();

		uint32_t prbs = rand();
		for(size_t i=0; i<depth; i++)
			dat->m_samples[i] = RunPRBS(prbs, poly);

		dat->m_samples.MarkModifiedFromCpu();
	}
}
