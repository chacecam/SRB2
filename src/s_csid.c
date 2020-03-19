// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 2017 by Mihaly "Hermit" Horvath.
//
// License: WTF - Do what the fuck you want with this code,
// but please mention me as its original author.
//-----------------------------------------------------------------------------
/// \file s_csid.h
/// \brief SID audio emulation
/// based on jsSID, this version has much lower CPU-usage, as mainloop runs at samplerate

#ifdef HAVE_CSID

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "doomdef.h"
#include "doomdata.h"
#include "doomtype.h"

#include "z_zone.h"
#include "v_video.h"

#include "s_sound.h"
#include "s_csid.h"

//global constants and variables
sidplayer_t sid;

static UINT8 *memory;

// raw output divided by this after multiplied by main volume, this also compensates for filter-resonance emphasis to avoid distotion
static int OUTPUT_SCALEDOWN = SID_CHANNEL_AMOUNT * 16 + 26;

enum { GATE_BITMASK=0x01, SYNC_BITMASK=0x02, RING_BITMASK=0x04, TEST_BITMASK=0x08,
							TRI_BITMASK=0x10, SAW_BITMASK=0x20, PULSE_BITMASK=0x40, NOISE_BITMASK=0x80,
							HOLDZERO_BITMASK=0x10, DECAYSUSTAIN_BITMASK=0x40, ATTACK_BITMASK=0x80,
							LOWPASS_BITMASK=0x10, BANDPASS_BITMASK=0x20, HIGHPASS_BITMASK=0x40, OFF3_BITMASK=0x80 };

static float clock_ratio = CLOCK_RATIO_DEFAULT;

// SID-emulation variables:
static const UINT8 FILTSW[9] = {1,2,4,1,2,4,1,2,4};
static UINT8 ADSRstate[9], expcnt[9], prevSR[9], sourceMSBrise[9];
static short int envcnt[9];
static unsigned int prevwfout[9], prevwavdata[9], sourceMSB[3], noise_LFSR[9];
static int phaseaccu[9], prevaccu[9], prevlowpass[3], prevbandpass[3];;
static float ratecnt[9], cutoff_ratio_8580, cutoff_steepness_6581, cap_6581_reciprocal; //, cutoff_ratio_6581, cutoff_bottom_6581, cutoff_top_6581;

// player-related variables:
static int sampleratio;
static UINT8 timermode[0x20];
static float framecnt = 0, frame_sampleperiod = SID_SAMPLERATE/PAL_FRAMERATE;

// CPU (and CIA/VIC-IRQ) emulation constants and variables - avoiding internal/automatic variables to retain speed
static const UINT8 flagsw[] = {0x01,0x21,0x04,0x24,0x00,0x40,0x08,0x28}, branchflag[] = {0x80,0x40,0x01,0x02};
static unsigned int PC=0, pPC=0, addr=0, storadd=0;
static short int A=0, T=0, SP=0xFF;
static UINT8 X=0, Y=0, IR=0, ST=0x00; // STATUS-flags: N V - B D I Z C
static float CPUtime=0.0;
static UINT8 cycles=0;
static boolean dynCIA = false;
static boolean finished = false;

static void cSID_init(void);
static void InitCPU(unsigned int mempos);
static void InitSIDChip(void);

static UINT8 CPUEmulate(void);
static int SIDEmulate(UINT8 num, unsigned int baseaddr);

static unsigned int combinedWF(UINT8 num, UINT8 channel, unsigned int* wfarray, int index, UINT8 differ6581, UINT8 freq);
static void createCombinedWF(unsigned int* wfarray, float bitmul, float bitstrength, float treshold);

//----------------------------- MAIN thread ----------------------------

void cSID_load(UINT8 *data, size_t length)
{
	unsigned int i, offs;
	int preferred_SID_model[3];

	CONS_Debug(DBG_AUDIO, "cSID_load %s\n", sizeu1(length));
	memcpy(&sid.filedata, data, length);

	offs = sid.filedata[7];
	sid.chip.loadaddr = sid.filedata[8] + sid.filedata[9] ? sid.filedata[8] * 256 + sid.filedata[9] : sid.filedata[offs] + sid.filedata[offs+1] * 256;
	CONS_Debug(DBG_AUDIO, "Offset: $%4.4X, load address: $%4.4X, size: $%4.4X\n", offs, sid.chip.loadaddr, (unsigned int)(length-offs));

	CONS_Debug(DBG_AUDIO, "Timermodes:");
	for (i = 0; i < 32; i++)
	{
		float modepow = pow(2, 7 - i % 8);
		timermode[31-i] = (sid.filedata[0x12+(i>>3)] & ((modepow > 0) ? 1 : 0));
		CONS_Debug(DBG_AUDIO, " %1d",timermode[31-i]);
	}
	CONS_Debug(DBG_AUDIO, "\n");

	// Init memory
	memory = sid.memory;
	for(i = 0; i < C64_MEMORY_SIZE; i++)
		memory[i] = 0x00;

	for (i = offs+2; i < length; i++)
	{
		size_t offset = sid.chip.loadaddr+i-(offs+2);
		if (offset < C64_MEMORY_SIZE)
			memory[offset] = sid.filedata[i];
	}

	// Lactozilla: I love macros!
#define copystring(dest, offs) \
	{ \
		int strend = 1; \
		for (i = 0; i < 32; i++) \
		{ \
			if (strend != 0) \
				strend = dest[i] = sid.filedata[offs+i]; \
			else \
				strend = dest[i] = 0; \
		} \
	}

	copystring(sid.title, 0x16);
	copystring(sid.author, 0x36);
	copystring(sid.info, 0x56);

#undef copystring

	CONS_Debug(DBG_AUDIO, "Title: %s\n", sid.title);
	CONS_Debug(DBG_AUDIO, "Author: %s\n", sid.author);
	CONS_Debug(DBG_AUDIO, "Info: %s\n", sid.info);

	sid.chip.initaddr = sid.filedata[0xA] + sid.filedata[0xB] ? (unsigned)(sid.filedata[0xA] * 256 + sid.filedata[0xB]) : sid.chip.loadaddr;
	sid.chip.playaddr = sid.chip.playaddf = sid.filedata[0xC] * 256 + sid.filedata[0xD];

	sid.subtune_amount = sid.filedata[0xF];
	CONS_Debug(DBG_AUDIO, "Subtune amount: %d\n", sid.subtune_amount);

	sid.chip.model[0] = sid.chip.model[1] = sid.chip.model[2] = 8580;
	preferred_SID_model[0] = (sid.filedata[0x77] & 0x30) >= 0x20 ? 8580 : 6581;
	preferred_SID_model[1] = (sid.filedata[0x77] & 0xC0) >= 0x80 ? 8580 : 6581;
	preferred_SID_model[2] = (sid.filedata[0x76] & 3) >= 2 ? 8580 : 6581;
	CONS_Debug(DBG_AUDIO, "Preferred SID model: %d\n", preferred_SID_model[0]);

	sid.chip.address[0] = SID_BASE_ADDRESS;
	sid.chip.address[1] = sid.filedata[0x7A] >= 0x42 && (sid.filedata[0x7A] < 0x80 || sid.filedata[0x7A] >= 0xE0) ? 0xD000 + sid.filedata[0x7A] * 16 : 0;
	sid.chip.address[2] = sid.filedata[0x7B] >= 0x42 && (sid.filedata[0x7B] < 0x80 || sid.filedata[0x7B] >= 0xE0) ? 0xD000 + sid.filedata[0x7B] * 16 : 0;

	sid.chip.amount = 1+(sid.chip.address[1] > 0)+(sid.chip.address[2] > 0);
	for (i = 0; i < (unsigned)sid.chip.amount; i++)
		sid.chip.model[i] = preferred_SID_model[i];
	CONS_Debug(DBG_AUDIO, "SID chip count: %d\n", sid.chip.amount);

	sampleratio = round(C64_PAL_CPUCLK/SID_SAMPLERATE);
	OUTPUT_SCALEDOWN = SID_CHANNEL_AMOUNT * 16 + 26;
	if (sid.chip.amount == 2)
		OUTPUT_SCALEDOWN /= 0.6;
	else if (sid.chip.amount >= 3)
		OUTPUT_SCALEDOWN /= 0.4;

	cSID_init();
}

void cSID_play(int track)
{
	static int timeout;

	if (track < 0 || track > 63 || track > sid.subtune_amount)
		track = 0;
	sid.subtune = track;

	InitCPU(sid.chip.initaddr);
	InitSIDChip();

	A = sid.subtune;
	memory[1] = 0x37;
	memory[0xDC05] = 0;

	// Lactozilla: What is this??
	for (timeout = 100000; timeout >= 0; timeout--)
	{
		if (CPUEmulate())
			break;
	}

	if (timermode[sid.subtune] || memory[0xDC05]) // CIA timing
	{
		if (!memory[0xDC05])
		{
			// C64 startup-default
			memory[0xDC04] = 0x24;
			memory[0xDC05] = 0x40;
		}
		frame_sampleperiod = (memory[0xDC04]+memory[0xDC05]*256)/clock_ratio;
	}
	else
		frame_sampleperiod = SID_SAMPLERATE/PAL_FRAMERATE;  // Vsync timing

	if (sid.chip.playaddf == 0)
		sid.chip.playaddr = ((memory[1]&3)<2)? memory[0xFFFE]+memory[0xFFFF]*256 : memory[0x314]+memory[0x315]*256;
	else
	{
		// player under KERNAL (Crystal Kingdom Dizzy)
		sid.chip.playaddr = sid.chip.playaddf;
		if (sid.chip.playaddr >= 0xE000 && memory[1] == 0x37)
			memory[1] = 0x35;
	}

	InitCPU(sid.chip.playaddr);
	framecnt = 1;
	finished = false;
	dynCIA = false;
	CPUtime = 0;
}

void cSID_mix(UINT8 *stream, int len) // called by SDL at samplerate pace
{
	static int i, output;

	for (i = 0; i < len; i += 2)
	{
		framecnt--;
		if (framecnt <= 0)
		{
			framecnt = frame_sampleperiod;
			finished = false;
			PC = sid.chip.playaddr;
			SP = 0xFF;
		}

		// Emulate the CPU
		if (!finished)
		{
			while (CPUtime <= clock_ratio)
			{
				pPC = PC;

				if (CPUEmulate() >= 0xFE || ((memory[1] & 3) > 1 && pPC < 0xE000 && (PC == 0xEA31 || PC == 0xEA81)))
				{
					finished = true;
					break;
				}
				else
					CPUtime += cycles; // RTS,RTI and IRQ player ROM return handling

				if ((addr == 0xDC05 || addr == 0xDC04) && (memory[1]&3) && timermode[sid.subtune])
				{
					// dynamic CIA-setting (Galway/Rubicon workaround)
					frame_sampleperiod = (memory[0xDC04] + memory[0xDC05]*256) / clock_ratio;
					if (!dynCIA)
					{
						dynCIA = true;
						CONS_Debug(DBG_AUDIO, "Dynamic CIA settings. New frame-sampleperiod: %.0f samples  (%.1fX speed)\n", round(frame_sampleperiod), SID_SAMPLERATE/PAL_FRAMERATE/frame_sampleperiod);
					}
				}

				// CJ in the USA workaround (writing above $d420, except SID2/SID3)
				if (storadd >= 0xD420 && storadd < 0xD800 && (memory[1] & 3))
				{
					// write to $D400..D41F if not in SID2/SID3 address-space
					if (!(sid.chip.address[1] <= storadd && storadd < sid.chip.address[1]+0x1F)
					&& !(sid.chip.address[2] <= storadd && storadd < sid.chip.address[2]+0x1F))
						memory[storadd&0xD41F] = memory[storadd];
				}

				// Whittaker player workarounds (if GATE-bit triggered too fast, 0 for some cycles then 1)
				if (addr==0xD404 && !(memory[0xD404]&GATE_BITMASK)) ADSRstate[0]&=0x3E;
				if (addr==0xD40B && !(memory[0xD40B]&GATE_BITMASK)) ADSRstate[1]&=0x3E;
				if (addr==0xD412 && !(memory[0xD412]&GATE_BITMASK)) ADSRstate[2]&=0x3E;
			}

			CPUtime -= clock_ratio;
		}

		// Mix SID chips
		output = SIDEmulate(0, sid.chip.address[0]); // 0xD400
		if (sid.chip.amount >= 2)
			output += SIDEmulate(1, sid.chip.address[1]);
		if (sid.chip.amount == 3)
			output += SIDEmulate(2, sid.chip.address[2]);

		stream[i] = (output & 0xFF);
		stream[i+1] = (output >> 8);
	}
}

void cSID_stop(void)
{
	memset(&sid.memory, 0x00, C64_MEMORY_SIZE);
	memset(&sid.filedata, 0x00, C64_MEMORY_SIZE);
	InitCPU(0);
	InitSIDChip();
}

//--------------------------------- CPU emulation -------------------------------------------

static void InitCPU(unsigned int mempos)
{
	PC = mempos; // Reset program counter
	SP = 0xFF; // Reset stack pointer
	A = 0; // Reset accumulator
	X = 0; // Reset X
	Y = 0; // Reset Y
	ST = 0; // Reset status
}

// My CPU implementation is based on the instruction table by Graham at codebase64.
// After some examination of the table it was clearly seen that columns of the table (instructions' 2nd nybbles)
// mainly correspond to addressing modes, and double-rows usually have the same instructions.
// The code below is laid out like this, with some exceptions present.
// Thanks to the hardware being in my mind when coding this, the illegal instructions could be added fairly easily...
static UINT8 CPUEmulate (void) // the CPU emulation for SID/PRG playback (ToDo: CIA/VIC-IRQ/NMI/RESET vectors, BCD-mode)
{
	// 'IR' is the instruction-register, naming after the hardware-equivalent
	IR = memory[PC];
	cycles = 2; // 'cycle': ensure smallest 6510 runtime (for implied/register instructions)
	storadd = 0;

	if (IR&1) // nybble2:  1/5/9/D:accu.instructions, 3/7/B/F:illegal opcodes
	{
		// addressing modes (begin with more complex cases), PC wraparound not handled inside to save codespace
		switch (IR & 0x1F)
		{
			// (zp,x)
			case 1:
			case 3:
				PC++;
				addr = memory[memory[PC]+X] + memory[memory[PC]+X+1]*256;
				cycles = 6;
				break;
			// (zp),y (5..6 cycles, 8 for R-M-W)
			case 0x11:
			case 0x13:
				PC++;
				addr = memory[memory[PC]] + memory[memory[PC]+1]*256 + Y;
				cycles = 6;
				break;
			// abs,y //(4..5 cycles, 7 cycles for R-M-W)
			case 0x19:
			case 0x1B:
				PC++;
				addr = memory[PC];
				PC++;
				addr += memory[PC]*256 + Y;
				cycles=5;
				break;
			//abs,x //(4..5 cycles, 7 cycles for R-M-W)
			case 0x1D:
				PC++;
				addr = memory[PC];
				PC++;
				addr += memory[PC]*256 + X;
				cycles = 5;
				break;
			// abs
			case 0xD:
			case 0xF:
				PC++;
				addr = memory[PC];
				PC++;
				addr += memory[PC]*256;
				cycles = 4;
				break;
			// zp,x
			case 0x15:
				PC++;
				addr = memory[PC] + X;
				cycles = 4;
				break;
			// zp
			case 5:
			case 7:
				PC++;
				addr = memory[PC];
				cycles = 3;
				break;
			case 0x17:
				PC++;
				// zp,x for illegal opcodes
				if ((IR&0xC0)!=0x80)
				{
					addr = memory[PC] + X;
					cycles = 4;
				}
				// zp,y for LAX/SAX illegal opcodes
				else
				{
					addr = memory[PC] + Y;
					cycles = 4;
				}
				break;
			case 0x1F:
				PC++;
				// abs,x for illegal opcodes
				if ((IR&0xC0)!=0x80)
				{
					// Lactozilla: probably incorrect
					addr = memory[PC] + memory[PC+1]*256 + X;
					PC++;
					cycles = 5;
				}
				// abs,y for LAX/SAX illegal opcodes
				else
				{
					// Lactozilla: probably incorrect
					addr = memory[PC] + memory[PC+1]*256 + Y;
					PC++;
					cycles = 5;
				}
				break;
			// immediate
			case 9:
			case 0xB:
				PC++;
				addr = PC;
				cycles = 2;
			default:
				break;
		}

		addr &= 0xFFFF;

		switch (IR & 0xE0)
		{
			case 0x60:
				if ((IR & 0x1F) != 0xB)
				{
					// ADC / RRA (ROR+ADC)
					if ((IR & 3) == 3)
					{
						T = (memory[addr] >> 1) + (ST & 1) * 128;
						ST &= 124;
						ST |= (T & 1);
						memory[addr] = T;
						cycles += 2;
					}
					T = A;
					A += memory[addr] + (ST & 1);
					ST &= 60;
					ST |= (A & 128) | (A > 255);
					A &= 0xFF;
					ST |= (!A) << 1 | ((!((T ^ memory[addr]) & 0x80)) & ((T ^ A) & 0x80)) >> 1;
				}
				else
				{
					A &= memory[addr];
					T += memory[addr] + (ST & 1);
					ST &= 60;
					ST |= (T>255) | ((!((A ^ memory[addr]) & 0x80)) & ((T ^ A) & 0x80)) >> 1; // V-flag set by intermediate ADC mechanism: (A&mem)+mem
					T = A;
					A = (A>>1)+(ST&1)*128;
					ST |= (A&128) | (T>127);
					ST |= (!A)<<1;
				}
				break; // ARR (AND+ROR, bit0 not going to C, but C and bit7 get exchanged.)
			case 0xE0:
				if ((IR & 3) == 3 && ((IR & 0x1F) != 0xB))
					memory[addr]++;
				cycles += 2;
				T = A;
				A -= memory[addr] + !(ST & 1); // SBC / ISC(ISB)=INC+SBC
				ST &= 60;
				ST |= (A & 128) | (A >= 0);
				A &= 0xFF;
				ST |= (!A)<<1 | (((T ^ memory[addr]) & 0x80) & ((T ^ A) & 0x80)) >> 1;
				break;
			case 0xC0:
				// CMP / DCP(DEC+CMP)
				if ((IR&0x1F)!=0xB)
				{
					if ((IR&3)==3)
					{
						memory[addr]--;
						cycles += 2;
					}
					T = A - memory[addr];
				}
				else // SBX(AXS)
					X = T = (A & X) - memory[addr];
				ST &= 124;
				ST |= (!(T & 0xFF)) << 1 | (T & 128) | (T >= 0);
				break;  // SBX (AXS) (CMP+DEX at the same time)
			case 0x00:
				// ORA / SLO(ASO)=ASL+ORA
				if ((IR&0x1F)!=0xB)
				{

					if ((IR&3)==3)
					{
						ST &= 124;
						ST |= (memory[addr] > 127);
						memory[addr] <<= 1;
						cycles += 2;
					}
					A |= memory[addr];
					ST &= 125;
					ST |= (!A) << 1 | (A & 128);
				}
				else // ANC (AND+Carry=bit7)
					A &= memory[addr];
				ST &= 124;
				ST |= (!A) << 1 | (A & 128) | (A>127);
				break;
			case 0x20:
				// AND / RLA (ROL+AND)
				if ((IR & 0x1F) != 0xB)
				{
					if ((IR & 3) == 3)
					{
						T = (memory[addr] << 1) + (ST & 1);
						ST &= 124;
						ST |= (T > 255);
						T &= 0xFF;
						memory[addr] = T;
						cycles += 2;
					}
					A &= memory[addr];
					ST &= 125;
					ST |= (!A) << 1 | (A & 128);
				}
				// ANC (AND+Carry=bit7)
				else
				{
					A &= memory[addr];
					ST &= 124;
					ST |= (!A) << 1 | (A & 128) | (A > 127);
				}
				break;
			case 0x40:
				// EOR / SRE(LSE)=LSR+EOR
				if ((IR & 0x1F) != 0xB)
				{
					if ((IR & 3) == 3)
					{
						ST &= 124;
						ST |= (memory[addr] & 1);
						memory[addr] >>= 1;
						cycles += 2;
					}
					A ^= memory[addr];
					ST &= 125;
					ST |= (!A) << 1 | (A & 128);
				}
				// ALR(ASR)=(AND+LSR)
				else
				{
					A &= memory[addr];
					ST &= 124;
					ST |= (A & 1);
					A >>= 1;
					A &= 0xFF;
					ST |= (A & 128) | ((!A) << 1);
				}
				break;
			case 0xA0:
				// LDA / LAX (illegal, used by my 1 rasterline player)
				if ((IR & 0x1F) != 0x1B)
				{
					A = memory[addr];
					if ((IR & 3) == 3)
						X=A;
				}
				else // LAS(LAR)
					A = X = SP = memory[addr] & SP;
				ST &= 125;
				ST |= ((!A) << 1) | (A & 128);
				break; // LAS (LAR)
			case 0x80:
				// XAA (TXA+AND), highly unstable on real 6502!
				if ((IR&0x1F)==0xB)
				{
					A = X & memory[addr];
					ST &= 125;
					ST |= (A & 128) | ((!A) << 1);
				}
				// TAS(SHS) (SP=A&X, mem=S&H} - unstable on real 6502
				else if ((IR&0x1F)==0x1B)
				{
					SP = A & X;
					memory[addr] = SP & ((addr >> 8) + 1);
				}
				// STA / SAX (at times same as AHX/SHX/SHY) (illegal)
				else
				{
					memory[addr] = A & (((IR & 3) == 3) ? X : 0xFF);
					storadd = addr;
				}
				break;
			default:
				break;
		}
	}
	else if (IR & 2) // nybble2:  2:illegal/LDX, 6:A/X/INC/DEC, A:Accu-shift/reg.transfer/NOP, E:shift/X/INC/DEC
	{
		switch (IR & 0x1F) // addressing modes
		{
			// abs,x / abs,y
			case 0x1E:
				PC++;
				addr = memory[PC];
				PC++;
				addr += memory[PC] * 256 + (((IR & 0xC0) != 0x80) ? X : Y);
				cycles = 5;
				break;
			// abs
			case 0xE:
				PC++;
				addr = memory[PC];
				PC++;
				addr += memory[PC] * 256;
				cycles = 4;
				break;
			// zp,x / zp,y
			case 0x16:
				PC++;
				addr = memory[PC] + (((IR & 0xC0) != 0x80) ? X : Y);
				cycles = 4;
				break;
			// zp
			case 6:
				PC++;
				addr = memory[PC];
				cycles = 3;
				break;
			// imm.
			case 2:
				PC++;
				addr = PC;
				cycles = 2;
			default:
				break;
		}

		addr &= 0xFFFF;

		switch (IR & 0xE0)
		{
			// Lactozilla: Fallthrough intentional
			case 0x00:
				ST &= 0xFE;
				/* FALLTHRU */
			case 0x20:
				// ASL/ROL (Accu)
				if ((IR & 0xF) == 0xA)
				{
					A = (A << 1) + (ST & 1);
					ST &= 124;
					ST |= (A & 128) | (A > 255);
					A &= 0xFF;
					ST |= (!A) << 1;
				}
				// RMW (Read-Write-Modify)
				else
				{
					T = (memory[addr] << 1) + (ST & 1);
					ST &= 124;
					ST |= (T & 128) | (T > 255);
					T &= 0xFF;
					ST |= (!T) << 1;
					memory[addr] = T;
					cycles += 2;
				}
				break;
			// Lactozilla: Fallthrough intentional
			case 0x40:
				ST &= 0xFE;
				/* FALLTHRU */
			case 0x60:
				//LSR/ROR (Accu)
				if ((IR & 0xF) == 0xA)
				{
					T = A;
					A = (A >> 1) + (ST & 1)*128;
					ST &= 124;
					ST |= (A & 128) | (T & 1);
					A &= 0xFF;
					ST |= (!A) << 1;
				}
				// memory (RMW)
				else
				{
					T = (memory[addr] >> 1) + (ST & 1)*128;
					ST &= 124;
					ST |= (T & 128) | (memory[addr] & 1);
					T &= 0xFF;
					ST |= (!T) << 1;
					memory[addr] = T;
					cycles += 2;
				}
				break;
			case 0xC0:
				// DEC
				if(IR&4)
				{
					memory[addr]--;
					ST &= 125;
					ST |= (!memory[addr]) << 1 | (memory[addr] & 128);
					cycles += 2;
				}
				// DEX
				else
				{
					X--;
					X &= 0xFF;
					ST &= 125;
					ST |= (!X) << 1 | (X & 128);
				}
				break;
			// LDX/TSX/TAX
			case 0xA0:
				if ((IR & 0xF) != 0xA)
					X = memory[addr];
				else if(IR&0x10)
				{
					X = SP;
					break;
				}
				else
					X = A;
				ST &= 125;
				ST |= (!X) << 1 | (X&128);
				break;
			// STX/TXS/TXA
			case 0x80:
				if (IR&4)
				{
					memory[addr] = X;
					storadd = addr;
				}
				else if(IR & 0x10)
					SP = X;
				else
				{
					A = X;
					ST &= 125;
					ST |= (!A) <<1 | (A & 128);
				}
				break;
			// INC/NOP
			case 0xE0:
				if(IR&4)
				{
					memory[addr]++;
					ST &= 125;
					ST |= (!memory[addr]) << 1 | (memory[addr] & 128);
					cycles += 2;
				}
				break;
			default:
				break;
		}
	}
	else if ((IR & 0xC) == 8) // nybble2:  8:register/status
	{
		switch (IR & 0xF0)
		{
			// PLA
			case 0x60:
				SP++;
				SP &= 0xFF;
				A = memory[0x100 + SP];
				ST &= 125;
				ST |= (!A) << 1 | (A & 128);
				cycles = 4;
				break;
			// INY
			case 0xC0:
				Y++;
				Y &= 0xFF;
				ST &= 125;
				ST |= (!Y) <<1 | (Y & 128);
				break;
			// INX
			case 0xE0:
				X++;
				X&=0xFF;
				ST&=125;
				ST|=(!X)<<1|(X&128); break;
			// DEY
			case 0x80:
				Y--;
				Y &= 0xFF;
				ST &= 125;
				ST |= (!Y) << 1 | (Y & 128);
				break;
			// PHP
			case 0x00:
				memory[0x100 + SP] = ST;
				SP--;
				SP &= 0xFF;
				cycles = 3;
				break;
			// PLP
			case 0x20:
				SP++;
				SP &= 0xFF;
				ST = memory[0x100 + SP];
				cycles = 4;
				break;
			// PHA
			case 0x40:
				memory[0x100 + SP] = A;
				SP--;
				SP &= 0xFF;
				cycles = 3;
				break;
			// TYA
			case 0x90:
				A = Y;
				ST &= 125;
				ST |= (!A) <<1 | (A & 128);
				break;
			// TAY
			case 0xA0:
				Y = A;
				ST &= 125;
				ST |= (!Y) << 1| (Y & 128);
				break;
			// CLC/SEC/CLI/SEI/CLV/CLD/SED
			default:
				if (flagsw[IR >> 5] & 0x20)
					ST |= (flagsw[IR >> 5] & 0xDF);
				else
					ST &= 255 - (flagsw[IR >> 5] & 0xDF);
			break;
		}
	}
	else // nybble2:  0: control/branch/Y/compare  4: Y/compare  C:Y/compare/JMP
	{
		if ((IR & 0x1F) == 0x10)
		{
			PC++;
			T = memory[PC];
			if (T & 0x80)
				T -= 0x100; //BPL/BMI/BVC/BVS/BCC/BCS/BNE/BEQ  relative branch
			if (IR & 0x20)
			{
				if (ST & branchflag[IR >> 6])
				{
					PC += T;
					cycles = 3;
				}
			}
			else
			{
				if (!(ST & branchflag[IR >> 6]))
				{
					PC += T;
					cycles = 3;
				}
			}
		}
		// nybble2:  0:Y/control/Y/compare  4:Y/compare  C:Y/compare/JMP
		else
		{
			// addressing modes
			switch (IR&0x1F)
			{
				// imm. (or abs.low for JSR/BRK)
				case 0:
					PC++;
					addr = PC;
					cycles = 2;
					break;
				// abs,x
				case 0x1C:
					PC++;
					addr = memory[PC];
					PC++;
					addr += memory[PC]*256 + X;
					cycles = 5;
					break;
				// abs
				case 0xC:
					PC++;
					addr = memory[PC];
					PC++;
					addr += memory[PC]*256;
					cycles = 4;
					break;
				// zp,x
				case 0x14:
					PC++;
					addr = memory[PC] + X;
					cycles = 4;
					break;
				// zp
				case 4:
					PC++;
					addr = memory[PC];
					cycles = 3;
				default:
					break;
			}

			addr &= 0xFFFF;

			switch (IR&0xE0)
			{
				// BRK
				case 0x00:
					memory[0x100 + SP] = PC % 256;
					SP--;
					SP &= 0xFF;
					memory[0x100 + SP] = PC / 256;
					SP--;
					SP &= 0xFF;
					memory[0x100 + SP] = ST;
					SP--;
					SP &= 0xFF;
					PC = memory[0xFFFE] + memory[0xFFFF] * 256-1;
					cycles = 7;
					break;
				case 0x20:
					// BIT
					if (IR & 0xF)
					{
						ST &= 0x3D;
						ST |= (memory[addr] & 0xC0) | (!(A & memory[addr])) << 1;
					}
					// JSR
					else
					{
						memory[0x100 + SP] = (PC + 2) % 256;
						SP--;
						SP &= 0xFF;
						memory[0x100 + SP] = (PC + 2) / 256;
						SP--;
						SP &= 0xFF;
						PC = memory[addr] + memory[addr+1]*256-1;
						cycles = 6;
					}
					break;
				case 0x40:
					// JMP
					if (IR & 0xF)
					{
						PC = addr-1;
						cycles = 3;
					}
					// RTI
					else
					{
						if (SP >= 0xFF)
							return 0xFE;
						SP++;
						SP &= 0xFF;
						ST = memory[0x100 + SP];
						SP++;
						SP &= 0xFF;
						T = memory[0x100 + SP];
						SP++;
						SP &= 0xFF;
						PC = memory[0x100 + SP] + T*256-1;
						cycles = 6;
					}
					break;
				case 0x60:
					// JMP() (indirect)
					if (IR & 0xF)
					{
						PC = memory[addr] + memory[addr+1] * 256-1;
						cycles = 5;
					}
					// RTS
					else
					{
						if (SP >= 0xFF)
							return 0xFF;
						SP++;
						SP &= 0xFF;
						T = memory[0x100 + SP];
						SP++;
						SP &= 0xFF;
						PC = memory[0x100 + SP] + T*256-1;
						cycles = 6;
					}
					break;
				// CPY
				case 0xC0:
					T = Y - memory[addr];
					ST &= 124;
					ST |= (!(T & 0xFF)) <<1 | (T & 128) | (T >= 0);
					break;
				// CPX
				case 0xE0:
					T = X - memory[addr];
					ST&=124;
					ST|=(!(T & 0xFF)) << 1 | (T & 128) | (T >= 0);
					break;
				// LDY
				case 0xA0:
					Y = memory[addr];
					ST &= 125;
					ST |= (!Y) << 1 | (Y & 128);
					break;
				// STY
				case 0x80:
					memory[addr] = Y;
					storadd = addr;
				default:
					break;
			}
		}
	}

	PC++;
	return 0;
}

//----------------------------- SID emulation -----------------------------------------

//Arrays to support the emulation:
static unsigned int TriSaw_8580[4096], PulseSaw_8580[4096], PulseTriSaw_8580[4096];
#define PERIOD0 CLOCK_RATIO_DEFAULT //max(round(clock_ratio),9)
#define STEP0 3 //ceil(PERIOD0/9.0)
static float ADSRperiods[16] = {PERIOD0, 32, 63, 95, 149, 220, 267, 313, 392, 977, 1954, 3126, 3907, 11720, 19532, 31251};
static UINT8 ADSRstep[16] =   {  STEP0, 1,  1,  1,  1,    1,   1,   1,   1,   1,    1,    1,    1,     1,     1,     1};
static const UINT8 ADSR_exptable[256] = {1, 30, 30, 30, 30, 30, 30, 16, 16, 16, 16, 16, 16, 16, 16, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4, 4, 4, 4, 4, //pos0:1  pos6:30  pos14:16  pos26:8
				4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, //pos54:4 //pos93:2
				1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
				1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
				1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };

static void cSID_init(void)
{
	int i;

	clock_ratio = C64_PAL_CPUCLK/SID_SAMPLERATE;
	if (clock_ratio > 9)
	{
		ADSRperiods[0] = clock_ratio;
		ADSRstep[0] = ceil(clock_ratio/9.0);
	}
	else
	{
		ADSRperiods[0] = 9.0;
		ADSRstep[0] = 1;
	}

	cutoff_ratio_8580 = -2 * 3.14 * (12500 / 2048) / SID_SAMPLERATE; //approx. 30Hz..12kHz according to datasheet, but only for 6.8nF value, 22nF makes 9Hz...3.7kHz? wrong
	cap_6581_reciprocal = -1000000/CAP_6581; //lighten CPU-load in sample-callback
	cutoff_steepness_6581 = FILTER_DARKNESS_6581*(2048.0-VCR_FET_TRESHOLD); //pre-scale for 0...2048 cutoff-value range //lighten CPU-load in sample-callback

	createCombinedWF(TriSaw_8580, 0.8, 2.4, 0.64);
	createCombinedWF(PulseSaw_8580, 1.4, 1.9, 0.68);
	createCombinedWF(PulseTriSaw_8580, 0.8, 2.5, 0.64);

	for (i = 0; i < 9; i++)
	{
		ADSRstate[i] = HOLDZERO_BITMASK; envcnt[i] = 0; ratecnt[i] = 0;
		phaseaccu[i] = 0; prevaccu[i] = 0; expcnt[i] = 0; prevSR[i]=0;
		noise_LFSR[i] = 0x7FFFFF; prevwfout[i] = 0;
	}
	for (i = 0; i < 3; i++)
	{
		sourceMSBrise[i] = 0; sourceMSB[i] = 0;
		prevlowpass[i] = 0; prevbandpass[i] = 0;
	}

	InitSIDChip();
}

//registers: 0:freql1  1:freqh1  2:pwml1  3:pwmh1  4:ctrl1  5:ad1   6:sr1  7:freql2  8:freqh2  9:pwml2 10:pwmh2 11:ctrl2 12:ad2  13:sr 14:freql3 15:freqh3 16:pwml3 17:pwmh3 18:ctrl3 19:ad3  20:sr3
//           21:cutoffl 22:cutoffh 23:flsw_reso 24:vol_ftype 25:potX 26:potY 27:OSC3 28:ENV3
static void InitSIDChip(void)
{
	int i;

	for (i = SID_BASE_ADDRESS; i <= SID_END_ADDRESS; i++)
		memory[i] = 0x00;
	for (i = 0xDE00; i <= 0xDFFF; i++)
		memory[i] = 0x00;

	for (i = 0; i < 9; i++)
	{
		ADSRstate[i] = HOLDZERO_BITMASK;
		ratecnt[i] = envcnt[i] = expcnt[i] = 0;
	}
}

// My SID implementation is similar to what I worked out in a SwinSID variant during 3..4 months of development. (So jsSID only took 2 weeks armed with this experience.)
// I learned the workings of ADSR/WAVE/filter operations mainly from the quite well documented resid and resid-fp codes.
// (The SID reverse-engineering sites were also good sources.)
// Note that I avoided many internal/automatic variables from the SID function, assuming better speed this way. (Not using stack as much, but I'm not sure and it may depend on platform...)
// (The same is true for CPU emulation and player-code.)
static int SIDEmulate(UINT8 num, unsigned int baseaddr) //the SID emulation itself ('num' is the number of SID to iterate (0..2)
{
	// better keep these variables static so they won't slow down the routine like if they were internal automatic variables always recreated
	static UINT8 channel, ctrl, SR, prevgate, wf, test, *sReg, *vReg;
	static unsigned int accuadd, MSB, pw, wfout;
	static int tmp, step, lim, nonfilt, filtin, filtout, output;
	static float period, steep, rDS_VCR_FET, cutoff[3], resonance[3], ftmp;

	filtin = nonfilt = 0;
	sReg = &memory[baseaddr];
	vReg = sReg;

	// treating 2SID and 3SID channels uniformly (0..5 / 0..8), this probably avoids some extra code
	for (channel = num * SID_CHANNEL_AMOUNT ; channel < (num + 1) * SID_CHANNEL_AMOUNT ; channel++, vReg += 7)
	{
		ctrl = vReg[4];

		// ADSR envelope-generator:
		SR = vReg[6];
		tmp = 0;
		prevgate = (ADSRstate[channel] & GATE_BITMASK);
		if (prevgate != (ctrl & GATE_BITMASK)) //gatebit-change?
		{
			if (prevgate)
				ADSRstate[channel] &= 0xFF - (GATE_BITMASK | ATTACK_BITMASK | DECAYSUSTAIN_BITMASK);
			// falling edge
			else
			{
				// rising edge, also sets hold_zero_bit=0
				ADSRstate[channel] = (GATE_BITMASK | ATTACK_BITMASK | DECAYSUSTAIN_BITMASK);
				// assume SR->GATE write order: workaround to have crisp soundstarts by triggering delay-bug
				// (this is for the possible missed CTRL(GATE) vs SR register write order situations (1MHz CPU is cca 20 times faster than samplerate)
				if ((SR & 0xF) > (prevSR[channel] & 0xF))
					tmp = 1;
			}
		}

		// assume SR->GATE write order: workaround to have crisp soundstarts by triggering delay-bug
		prevSR[channel] = SR;
		ratecnt[channel] += clock_ratio;
		if (ratecnt[channel] >= 0x8000)
			ratecnt[channel] -= 0x8000; // can wrap around (ADSR delay-bug: short 1st frame)

		// set ADSR period that should be checked against rate-counter (depending on ADSR state Attack/DecaySustain/Release)
		if (ADSRstate[channel] & ATTACK_BITMASK)
			step = vReg[5] >> 4;
		else if (ADSRstate[channel] & DECAYSUSTAIN_BITMASK)
			step = vReg[5] & 0xF;
		else
			step = SR & 0xF;

		period = ADSRperiods[step];
		step = ADSRstep[step];

		// ratecounter shot (matches rateperiod) (in genuine SID ratecounter is LFSR)
		if (ratecnt[channel] >= period && ratecnt[channel] < period + clock_ratio && tmp == 0)
		{
			ratecnt[channel] -= period; // compensation for timing instead of simply setting 0 on rate-counter overload
			if ((ADSRstate[channel] & ATTACK_BITMASK) || ++expcnt[channel] == ADSR_exptable[envcnt[channel]])
			{
				if (!(ADSRstate[channel] & HOLDZERO_BITMASK))
				{
					if (ADSRstate[channel] & ATTACK_BITMASK)
					{
						envcnt[channel] += step;
						if (envcnt[channel] >= 0xFF)
						{
							envcnt[channel] = 0xFF;
							ADSRstate[channel] &= 0xFF-ATTACK_BITMASK;
						}
					}
					else if (!(ADSRstate[channel] & DECAYSUSTAIN_BITMASK) || envcnt[channel] > (SR&0xF0) + (SR>>4))
					{
						envcnt[channel] -= step;
						if (envcnt[channel] <= 0 && envcnt[channel]+step != 0)
						{
							envcnt[channel] = 0;
							ADSRstate[channel] |= HOLDZERO_BITMASK;
						}
					}
				}
				expcnt[channel] = 0;
			}
		}

		envcnt[channel] &= 0xFF;

		// WAVE-generation code (phase accumulator and waveform-selector):
		test = ctrl & TEST_BITMASK;  wf = ctrl & 0xF0;  accuadd = (vReg[0] + vReg[1] * 256) * clock_ratio;
		if (test || ((ctrl & SYNC_BITMASK) && sourceMSBrise[num]))
			phaseaccu[channel] = 0;
		else
		{
			phaseaccu[channel] += accuadd;
			if (phaseaccu[channel] > 0xFFFFFF)
				phaseaccu[channel] -= 0x1000000;
		}

		phaseaccu[channel] &= 0xFFFFFF;
		MSB = phaseaccu[channel] & 0x800000;
		sourceMSBrise[num] = (MSB > (prevaccu[channel] & 0x800000)) ? 1 : 0;

		// noise waveform
		if (wf & NOISE_BITMASK)
		{
			tmp = noise_LFSR[channel];
			// clock LFSR all time if clockrate exceeds observable at given samplerate
			if (((phaseaccu[channel] & 0x100000) != (prevaccu[channel] & 0x100000)) || accuadd >= 0x100000)
			{
				step = (tmp & 0x400000) ^ ((tmp & 0x20000) << 5);
				tmp = ((tmp << 1) + (step ? 1 : test)) & 0x7FFFFF;
				noise_LFSR[channel] = tmp;
			}
			// we simply zero output when other waveform is mixed with noise.
			// On real SID LFSR continuously gets filled by zero and locks up.
			// ($C1 waveform with pw<8 can keep it for a while...)
			wfout = (wf & 0x70) ? 0 : ((tmp & 0x100000) >> 5) + ((tmp & 0x40000) >> 4) + ((tmp & 0x4000) >> 1) + ((tmp & 0x800) << 1) + ((tmp & 0x200) << 2) + ((tmp & 0x20) << 5) + ((tmp & 0x04) << 7) + ((tmp & 0x01) << 8);
		}
		// simple pulse
		else if (wf & PULSE_BITMASK)
		{
			pw = (vReg[2] + (vReg[3] & 0xF) * 256) * 16;
			tmp = (int)accuadd >> 9;
			if (0 < (signed)pw && (signed)pw < tmp)
				pw = (unsigned)tmp;
			tmp ^= 0xFFFF;
			if ((signed)pw > tmp)
				pw = (unsigned)tmp;
			tmp = phaseaccu[channel] >> 8;

			// simple pulse, most often used waveform, make it sound as clean as possible without oversampling
			if (wf == PULSE_BITMASK)
			{
				// One of my biggest success with the SwinSID-variant was that I could clean the high-pitched and thin sounds.
				// (You might have faced with the unpleasant sound quality of high-pitched sounds without oversampling.
				// We need so-called 'band-limited' synthesis instead.
				// There are a lot of articles about this issue on the internet.
				// In a nutshell, the harsh edges produce harmonics that exceed the
				// Nyquist frequency (samplerate/2) and they are folded back into hearable range,
				// producing unvanted ringmodulation-like effect.)
				// After so many trials with dithering/filtering/oversampling/etc.
				// it turned out I can't eliminate the fukkin aliasing in time-domain, as suggested at pages.
				// Oversampling (running the wave-generation 8 times more) was not a way at 32MHz SwinSID.
				// It might be an option on PC but I don't prefer it in JavaScript.)
				// The only solution that worked for me in the end, what I came up with eventually:
				// The harsh rising and falling edges of the pulse are elongated making it a bit trapezoid.
				// But not in time-domain, but altering the transfer-characteristics.
				// This had to be done in a frequency-dependent way, proportionally to pitch, to keep the deep sounds crisp.
				// The following code does this (my favourite testcase is Robocop3 intro):
				step = (accuadd >= 255) ? 65535 / (accuadd / 256.0) : 0xFFFF;
				if (test)
					wfout = 0xFFFF;
				else if (tmp < (signed)pw)
				{
					// rising edge
					lim = (0xFFFF - pw) * step;
					if (lim > 0xFFFF)
						lim = 0xFFFF;
					tmp = lim - (pw - tmp) * step;
					wfout = (tmp < 0) ? 0 : tmp;
				}
				else
				{
					// falling edge
					lim = pw * step;
					if (lim > 0xFFFF)
						lim = 0xFFFF;
					tmp = (0xFFFF - tmp) * step - lim;
					wfout = (tmp >= 0) ? 0xFFFF : tmp;
				}
			}
			// combined pulse
			else
			{
				wfout = (tmp >= (signed)pw || test) ? 0xFFFF : 0; // (this would be enough for a simple but aliased-at-high-pitches pulse)
				if (wf&TRI_BITMASK)
				{
					// pulse+saw+triangle (waveform nearly identical to tri+saw)
					if (wf & SAW_BITMASK)
						wfout = wfout ? combinedWF(num, channel, PulseTriSaw_8580, tmp>>4, 1, vReg[1]) : 0;
					// pulse+triangle
					else
					{
						tmp = phaseaccu[channel] ^ (ctrl & RING_BITMASK ? sourceMSB[num]:0);
						wfout = (wfout)? combinedWF(num, channel, PulseSaw_8580, (tmp ^ (tmp & 0x800000 ? 0xFFFFFF : 0))>>11, 0, vReg[1]) : 0;
					}
				}
				// pulse+saw
				else if (wf & SAW_BITMASK)
					wfout = wfout ? combinedWF(num, channel, PulseSaw_8580, tmp>>4, 1, vReg[1]) : 0;
			}
		}
		// saw
		else if (wf&SAW_BITMASK)
		{
			// saw (this row would be enough for simple but aliased-at-high-pitch saw)
			wfout = phaseaccu[channel]>>8;
			// The anti-aliasing (cleaning) of high-pitched sawtooth wave works by the same principle as mentioned above for the pulse,
			// but the sawtooth has even harsher edge/transition, and as the falling edge gets longer, tha rising edge should became shorter,
			// and to keep the amplitude, it should be multiplied a little bit (with reciprocal of rising-edge steepness).
			// The waveform at the output essentially becomes an asymmetric triangle, more-and-more approaching symmetric shape towards high frequencies.
			// (If you check a recording from the real SID, you can see a similar shape, the high-pitch sawtooth waves are triangle-like...)
			// But for deep sounds the sawtooth is really close to a sawtooth, as there is no aliasing there, but deep sounds should be sharp...
			if (wf&TRI_BITMASK) // saw+triangle
				wfout = combinedWF(num, channel, TriSaw_8580, wfout>>4, 1, vReg[1]);
			else
			// simple cleaned (bandlimited) saw
			{
				steep = (accuadd/65536.0)/288.0;
				wfout += wfout*steep;
				if(wfout>0xFFFF)
					wfout=0xFFFF-(wfout-0x10000)/steep;
			}
		}
		// triangle (this waveform has no harsh edges, so it doesn't suffer from strong aliasing at high pitches)
		else if (wf&TRI_BITMASK)
		{
			tmp = phaseaccu[channel]^(ctrl & RING_BITMASK ? sourceMSB[num] : 0);
			wfout = (tmp^(tmp & 0x800000 ? 0xFFFFFF : 0)) >> 7;
		}

		wfout &= 0xFFFF;
		if (wf)
			prevwfout[channel] = wfout;
		// emulate waveform 00 floating wave-DAC (on real SID waveform00 decays after 15s..50s depending on temperature?)
		else
			wfout = prevwfout[channel];
		prevaccu[channel] = phaseaccu[channel];
		sourceMSB[num] = MSB; // (So the decay is not an exact value. Anyway, we just simply keep the value to avoid clicks and support SounDemon digi later...)

		//routing the channel signal to either the filter or the unfiltered master output depending on filter-switch SID-registers
		if (sReg[0x17] & FILTSW[channel])
			filtin += ((int)wfout - 0x8000) * envcnt[channel] / 256;
		else if ((FILTSW[channel] != 4) || !(sReg[0x18] & OFF3_BITMASK))
			nonfilt += ((int)wfout - 0x8000) * envcnt[channel] / 256;
	}

	// update readable SID1-registers (some SID tunes might use 3rd channel ENV3/OSC3 value as control)
	// Lactozilla: ?????????? "left-hand operand of comma expression has no effect"
	if (num == 0) //, memory[1] & 3)
	{
		sReg[0x1B] = wfout >> 8;
		sReg[0x1C] = envcnt[3]; // OSC3, ENV3 (some players rely on it)
	}

	// FILTER: two integrator loop bi-quadratic filter, workings learned from resid code, but I kindof simplified the equations
	// The phases of lowpass and highpass outputs are inverted compared to the input, but bandpass IS in phase with the input signal.
	// The 8580 cutoff frequency control-curve is ideal (binary-weighted resistor-ladder VCRs), while the 6581 has a treshold, and below that it
	// outputs a constant ~200Hz cutoff frequency. (6581 uses MOSFETs as VCRs to control cutoff causing nonlinearity and some 'distortion' due to resistance-modulation.
	// There's a cca. 1.53Mohm resistor in parallel with the MOSFET in 6581 which doesn't let the frequency go below 200..220Hz
	// Even if the MOSFET doesn't conduct at all. 470pF capacitors are small, so 6581 can't go below this cutoff-frequency with 1.5MOhm.)
	cutoff[num] = sReg[0x16] * 8 + (sReg[0x15] & 7);
	if (sid.chip.model[num] == 8580)
	{
		cutoff[num] = (1 - exp((cutoff[num]+2) * cutoff_ratio_8580)); // linear curve by resistor-ladder VCR
		resonance[num] = (pow(2, ((4 - (sReg[0x17] >> 4)) / 8.0)));
	}
	else
	{
		// 6581
		cutoff[num] += round(filtin*FILTER_DISTORTION_6581); // MOSFET-VCR control-voltage-modulation (resistance-modulation aka 6581 filter distortion) emulation
		rDS_VCR_FET = cutoff[num]<=VCR_FET_TRESHOLD ? 100000000.0 // below Vth treshold Vgs control-voltage FET presents an open circuit
			: cutoff_steepness_6581/(cutoff[num]-VCR_FET_TRESHOLD); // rDS ~ (-Vth*rDSon) / (Vgs-Vth)  //above Vth FET drain-source resistance is proportional to reciprocal of cutoff-control voltage
		cutoff[num] = (1 - exp(cap_6581_reciprocal / (VCR_SHUNT_6581*rDS_VCR_FET/(VCR_SHUNT_6581+rDS_VCR_FET)) / SID_SAMPLERATE)); //curve with 1.5MOhm VCR parallel Rshunt emulation
		resonance[num] = ((sReg[0x17] > 0x5F) ? 8.0 / (sReg[0x17] >> 4) : 1.41);
	}

	filtout = 0;
	ftmp = filtin + prevbandpass[num] * resonance[num] + prevlowpass[num];
	if (sReg[0x18] & HIGHPASS_BITMASK)
		filtout -= ftmp;

	ftmp = prevbandpass[num] - ftmp * cutoff[num];
	prevbandpass[num] = ftmp;
	if (sReg[0x18] & BANDPASS_BITMASK)
		filtout -= ftmp;

	ftmp = prevlowpass[num] + ftmp * cutoff[num];
	prevlowpass[num] = ftmp;
	if (sReg[0x18] & LOWPASS_BITMASK)
		filtout += ftmp;

	// output stage for one SID
	// when it comes to $D418 volume-register digi playback, I made an AC / DC separation for $D418 value in the SwinSID at low (20Hz or so) cutoff-frequency,
	// and sent the AC (highpass) value to a 4th 'digi' channel mixed to the master output, and set ONLY the DC (lowpass) value to the volume-control.
	// This solved 2 issues: Thanks to the lowpass filtering of the volume-control, SID tunes where digi is played together with normal SID channels,
	// won't sound distorted anymore, and the volume-clicks disappear when setting SID-volume. (This is useful for fade-in/out tunes like Hades Nebula, where clicking ruins the intro.)
	output = (nonfilt+filtout) * (sReg[0x18] & 0xF) / OUTPUT_SCALEDOWN;
	if (output >= 32767)
		output = 32767;
	else if (output <= -32768)
		output = -32768; // saturation logic on overload (not needed if the callback handles it)
	return (int)output; // master output
}

// The anatomy of combined waveforms: The resid source simply uses 4kbyte 8bit samples from wavetable arrays, says these waveforms are mystic due to the analog behaviour.
// It's true, the analog things inside SID play a significant role in how the combined waveforms look like, but process variations are not so huge that cause much differences in SIDs.
// After checking these waveforms by eyes, it turned out for me that these waveform are fractal-like, recursively approachable waveforms.
// My 1st thought and trial was to store only a portion of the waveforms in table, and magnify them depending on phase-accumulator's state.
// But I wanted to understand how these waveforms are produced. I felt from the waveform-diagrams that the bits of the waveforms affect each other,
// hence the recursive look. A short C code proved by assumption, I could generate something like a pulse+saw combined waveform.
// Recursive calculations were not feasible for MCU of SwinSID, but for jsSID I could utilize what I found out and code below generates the combined waveforms into wavetables.
// To approach the combined waveforms as much as possible, I checked out the SID schematic that can be found at some reverse-engineering sites...
// The SID's R-2R ladder WAVE DAC is driven by operation-amplifier like complementary FET output drivers, so that's not the place where I first thought the magic happens.
// These 'opamps' (for all 12 wave-bits) have single FETs as inputs, and they switch on above a certain level of input-voltage, causing 0 or 1 bit as R-2R DAC input.
// So the first keyword for the workings is TRESHOLD. These FET inputs are driven through serial switch FETs (wave-selector) that normally enables one waveform at a time.
// The phase-accumulator's output is brought to 3 kinds of circuitries for the 3 basic waveforms. The pulse simply drives
// all wave-selector inputs with a 0/1 depending on pulsewidth, the sawtooth has a XOR for triangle/ringmod generation, but what
// is common for all waveforms, they have an open-drain driver before the wave-selector, which has FETs towards GND and 'FET resistor' towards the power-supply rail.
// These outputs are clearly not designed to drive high loads, and normally they only have to drive the FETs input mentioned above.
// But when more of these output drivers are switched together by the switch-FETs in the wave-selector, they affect each other by loading each other.
// The pulse waveform, when selected, connects all of them together through a fairly strong connection, and its signal also affects the analog level (pulls below the treshold)...
// The farther a specific DAC bit driver is from the other, the less it affects its output. It turned out it's not powers of 2 but something else,
// that creates similar combined waveforms to that of real SID's... Note that combined waveforms never have values bigger than their sourcing sawtooth wave.
// The analog levels that get generated by the various bit drivers, that pull each other up/DOWN, depend on the resistances the components/wires inside the SID.
// And finally, what is output on the DAC depends on whether these analog levels are below or above the FET gate's treshold-level,
// That's how the combined waveform is generated. Maybe I couldn't explain well enough, but the code below is simple enough to understand the mechanism algoritmically.
// This simplified schematic exapmle might make it easier to understand sawtooth+pulse combination (must be observed with monospace fonts):
//                               _____            |-    .--------------.   /\/\--.
// Vsupply                /  .----| |---------*---|-    /    Vsupply   !    R    !      As can be seen on this schematic,
//  ------.       other   !  !   _____        !  TRES   \       \      !         /      the pulse wave-selector FETs
//        !       saw bit *--!----| |---------'  HOLD   /       !     |-     2R  \      connect the neighbouring sawtooth
//        /       output  !  !                          !      |------|-         /      outputs with a fairly strong
//     Rd \              |-  !WAVEFORM-SELECTOR         *--*---|-      !    R    !      connection to each other through
//        /              |-  !SWITCHING FETs            !  !    !      *---/\/\--*      their own wave-selector FETs.
//        ! saw-bit          !    _____                |-  !   ---     !         !      So the adjacent sawtooth outputs
//        *------------------!-----| |-----------*-----|-  !          |-         /      pull each other lower (or maybe a bit upper but not exceeding sawtooth line)
//        ! (weak drive,so   !  saw switch       ! TRES-!  `----------|-     2R  \      depending on their low/high state and
//       |- can be shifted   !                   ! HOLD !              !         /      distance from each other, causing
//  -----|- down (& up?)     !    _____          !      !              !     R   !      the resulting analog level that
//        ! by neighbours)   *-----| |-----------'     ---            ---   /\/\-*      will either turn the output on or not.
//   GND ---                 !  pulse switch                                     !      (Depending on their relation to treshold.)
//
// (As triangle waveform connects adjacent bits by default, the above explained effect becomes even stronger, that's why combined waveforms with thriangle are at 0 level most of the time.)

// in case you don't like these calculated combined waveforms it's easy to substitute the generated tables by pre-sampled 'exact' versions
static unsigned int combinedWF(UINT8 num, UINT8 channel, unsigned int* wfarray, int index, UINT8 differ6581, UINT8 freqh)
{
	static float addf;
	addf = 0.6+0.4/freqh;
	if (differ6581 && sid.chip.model[num] == 6581)
		index &= 0x7FF;
	prevwavdata[channel] = wfarray[index]*addf + prevwavdata[channel]*(1.0-addf);
	return prevwavdata[channel];
}

static void createCombinedWF(unsigned int* wfarray, float bitmul, float bitstrength, float treshold)
{
	int i, j, k;
	for (i = 0; i < 4096; i++)
	{
		wfarray[i] = 0;
		for (j = 0; j < 12; j++)
		{
			float bitlevel = 0;
			for (k = 0; k < 12; k++)
				bitlevel += (bitmul / pow(bitstrength, fabs((float)(k-j)))) * (((i>>k)&1)-0.5);
			wfarray[i] += (bitlevel >= treshold) ? pow(2, j) : 0;
		}
		wfarray[i] *= 12;
	}
}

#endif
