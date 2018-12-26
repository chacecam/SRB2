// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2016 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  r_draw.h
/// \brief Low-level span/column drawer functions

#ifndef __R_DRAW__
#define __R_DRAW__

#include "r_defs.h"

// -------------------------------
//      COMMON STUFF FOR 32bpp
// -------------------------------
extern UINT32 *ylookup[MAXVIDHEIGHT*4];
extern UINT32 *ylookup1[MAXVIDHEIGHT*4];
extern UINT32 *ylookup2[MAXVIDHEIGHT*4];
extern INT32 columnofs[MAXVIDWIDTH*4];
extern UINT32 *topleft;

// -------------------------
// COLUMN DRAWING CODE STUFF
// -------------------------

extern INT32 dc_x, dc_yl, dc_yh;
extern fixed_t dc_iscale, dc_texturemid;
extern UINT8 dc_hires;

extern UINT8 *dc_source; // first pixel in a column

// Jimita: True-color
extern lighttable_t   *dc_colormap;
extern lighttable32_t *dc_truecolormap;

// translucency stuff here
#define NUMTRANSTABLES 9 // how many translucency tables are used
extern UINT8 *transtables;

// Jimita: True-color
extern UINT8 dc_transmap;
extern UINT32 dc_foglight;

// translation stuff here

extern UINT8 *dc_translation;

extern struct r_lightlist_s *dc_lightlist;
extern INT32 dc_numlights, dc_maxlights;

//Fix TUTIFRUTI
extern INT32 dc_texheight;

// -----------------------
// SPAN DRAWING CODE STUFF
// -----------------------

extern INT32 ds_y, ds_x1, ds_x2;
extern fixed_t ds_xfrac, ds_yfrac, ds_xstep, ds_ystep;

extern UINT8 *ds_source;

// Jimita: True-color
extern lighttable_t *ds_colormap;
extern UINT32 *ds_truecolormap;

extern UINT8 ds_transmap;
extern UINT32 ds_foglight;

#ifdef ESLOPE
typedef struct {
	float x, y, z;
} floatv3_t;

extern floatv3_t ds_su, ds_sv, ds_sz; // Vectors for... stuff?
extern float focallengthf, zeroheight;
#endif

// Variable flat sizes
extern UINT32 nflatxshift;
extern UINT32 nflatyshift;
extern UINT32 nflatshiftup;
extern UINT32 nflatmask;

// ===================================
//  r_draw.c COMMON ROUTINES FOR 8bpp
// ===================================

#define GTC_CACHE 1

#define TC_DEFAULT    -1
#define TC_BOSS       -2
#define TC_METALSONIC -3 // For Metal Sonic battle
#define TC_ALLWHITE   -4 // For Cy-Brak-demon

// Initialize color translation tables, for player rendering etc.
void R_InitTranslationTables(void);
UINT8* R_GetTranslationColormap(INT32 skinnum, skincolors_t color, UINT8 flags);
void R_FlushTranslationColormapCache(void);
UINT8 R_GetColorByName(const char *name);

// -----------------
// 32bpp DRAWING CODE
// -----------------

// Columns
void R_DrawColumn_32(void);
#define R_DrawWallColumn_32	R_DrawColumn_32
void R_DrawShadeColumn_32(void);
void R_DrawTranslucentColumn_32(void);

void R_Draw2sMultiPatchColumn_32(void);
void R_Draw2sMultiPatchTranslucentColumn_32(void);
void R_DrawFogSpan_32(void);
void R_DrawFogColumn_32(void);
void R_DrawColumnShadowed_32(void);

void R_DrawTranslatedColumn_32(void);
void R_DrawTranslatedTranslucentColumn_32(void);

// Spans
void R_DrawSpan_32(void);
void R_DrawSplat_32(void);
void R_DrawTranslucentSplat_32(void);
void R_DrawTranslucentSpan_32(void);

#ifndef NOWATER
void R_DrawTranslucentWaterSpan_32(void);
extern INT32 ds_bgoffset;
extern INT32 ds_wateroffset;
extern INT32 ds_watertimer;
#endif

// Tilted spans
#ifdef ESLOPE
void R_CalcTiltedLighting(fixed_t start, fixed_t end);
void R_DrawTiltedSpan_32(void);
void R_DrawTiltedTranslucentSpan_32(void);
void R_DrawTiltedSplat_32(void);
#endif

// (Unused)
#ifdef USEASM
void ASMCALL R_DrawColumn_8_ASM(void);
#define R_DrawWallColumn_8_ASM R_DrawColumn_8_ASM
void ASMCALL R_DrawShadeColumn_8_ASM(void);
void ASMCALL R_DrawTranslucentColumn_8_ASM(void);
void ASMCALL R_Draw2sMultiPatchColumn_8_ASM(void);

void ASMCALL R_DrawColumn_8_MMX(void);
#define R_DrawWallColumn_8_MMX R_DrawColumn_8_MMX

void ASMCALL R_Draw2sMultiPatchColumn_8_MMX(void);
void ASMCALL R_DrawSpan_8_MMX(void);
#endif

// =========================================================================
#endif  // __R_DRAW__
