// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 2017 by Mihaly "Hermit" Horvath.
//
// License: WTF - Do what the fuck you want with this code,
// but please mention me as its original author.
//-----------------------------------------------------------------------------
/// \file s_csid.h
/// \brief SID audio emulation

#ifndef __S_CSID__
#define __S_CSID__

#ifdef HAVE_CSID

#include "doomdef.h"
#include "doomdata.h"
#include "doomtype.h"

#include "s_sound.h"

#define C64_MEMORY_SIZE 0xFFFF
#define C64_PAL_CPUCLK 985248.0

#define SID_BASE_ADDRESS 0xD400
#define SID_END_ADDRESS 0xD7FF
#define SID_CHANNEL_AMOUNT 3
#define SID_SAMPLERATE (44100.0 * 2) // Lactozilla: changed to * 2

#define PAL_FRAMERATE 50.06 //50.0443427 //50.1245419 //(C64_PAL_CPUCLK/63/312.5), selected carefully otherwise some ADSR-sensitive tunes may suffer more:
#define CLOCK_RATIO_DEFAULT C64_PAL_CPUCLK/SID_SAMPLERATE  //(50.0567520: lowest framerate where Sanxion is fine, and highest where Myth is almost fine)
#define VCR_SHUNT_6581 1500 //kOhm //cca 1.5 MOhm Rshunt across VCR FET drain and source (causing 220Hz bottom cutoff with 470pF integrator capacitors in old C64)
#define VCR_FET_TRESHOLD 192 //Vth (on cutoff numeric range 0..2048) for the VCR cutoff-frequency control FET below which it doesn't conduct
#define CAP_6581 0.470 //nF //filter capacitor value for 6581
#define FILTER_DARKNESS_6581 22.0 //the bigger the value, the darker the filter control is (that is, cutoff frequency increases less with the same cutoff-value)
#define FILTER_DISTORTION_6581 0.0016 //the bigger the value the more of resistance-modulation (filter distortion) is applied for 6581 cutoff-control

typedef struct
{
	boolean playing;

	// Song info
	char title[0x20], author[0x20], info[0x20];
	int subtune;
	int subtune_amount;

	// SID chip things
	struct {
		int amount;
		int model[3];
		unsigned int address[3];
		unsigned int loadaddr, initaddr, playaddr, playaddf;
	} chip;

	// CPU memory and SID data
	UINT8 filedata[C64_MEMORY_SIZE];
	UINT8 memory[C64_MEMORY_SIZE];

	// Mixing
	int buffer_length;
	INT16 *stream;
} sidplayer_t;
extern sidplayer_t sid;

void cSID_load(UINT8 *data, size_t length);
void cSID_play(int track);
void cSID_mix(UINT8 *stream, int len);
void cSID_stop(void);

#endif // HAVE_CSID

#endif
