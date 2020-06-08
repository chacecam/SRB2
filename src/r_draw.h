// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  r_draw.h
/// \brief Video buffer and color translation functions.

#ifndef __R_DRAW__
#define __R_DRAW__

#include "r_defs.h"

#include "swrenderer/sw_draw.h"

/// \brief Top border
#define BRDR_T 0
/// \brief Bottom border
#define BRDR_B 1
/// \brief Left border
#define BRDR_L 2
/// \brief Right border
#define BRDR_R 3
/// \brief Topleft border
#define BRDR_TL 4
/// \brief Topright border
#define BRDR_TR 5
/// \brief Bottomleft border
#define BRDR_BL 6
/// \brief Bottomright border
#define BRDR_BR 7

extern lumpnum_t viewborderlump[8];

// ------------------------------------------------
// r_draw.c COMMON ROUTINES FOR BOTH 8bpp and 16bpp
// ------------------------------------------------

#define GTC_CACHE 1

#define TC_DEFAULT    -1
#define TC_BOSS       -2
#define TC_METALSONIC -3 // For Metal Sonic battle
#define TC_ALLWHITE   -4 // For Cy-Brak-demon
#define TC_RAINBOW    -5 // For single colour
#define TC_BLINK      -6 // For item blinking, according to kart
#define TC_DASHMODE   -7 // For Metal Sonic's dashmode

UINT8* R_GetTranslationColormap(INT32 skinnum, skincolors_t color, UINT8 flags);
void R_FlushTranslationColormapCache(void);
UINT8 R_GetColorByName(const char *name);
UINT8 R_GetSuperColorByName(const char *name);

void R_InitViewBorder(void);

// Rendering function.
#if 0
void R_FillBackScreen(void);

// If the view size is not full screen, draws a border around it.
void R_DrawViewBorder(void);
#endif

#endif  // __R_DRAW__
