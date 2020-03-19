// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 2017 by Mihaly "Hermit" Horvath.
//
// License: WTF - Do what the fuck you want with this code,
// but please mention me as its original author.
//-----------------------------------------------------------------------------
/// \file s_csid.h
/// \brief cSID player

#ifndef __S_CSID__
#define __S_CSID__

#ifdef HAVE_C64SID

#include "../doomdef.h"
#include "../doomdata.h"
#include "../doomtype.h"

#include "../s_sound.h"

#define VCR_SHUNT_6581 1500 //kOhm //cca 1.5 MOhm Rshunt across VCR FET drain and source (causing 220Hz bottom cutoff with 470pF integrator capacitors in old C64)
#define VCR_FET_TRESHOLD 192 //Vth (on cutoff numeric range 0..2048) for the VCR cutoff-frequency control FET below which it doesn't conduct
#define CAP_6581 0.470 //nF //filter capacitor value for 6581
#define FILTER_DARKNESS_6581 22.0 //the bigger the value, the darker the filter control is (that is, cutoff frequency increases less with the same cutoff-value)
#define FILTER_DISTORTION_6581 0.0016 //the bigger the value the more of resistance-modulation (filter distortion) is applied for 6581 cutoff-control

#endif // HAVE_CSID

#endif
