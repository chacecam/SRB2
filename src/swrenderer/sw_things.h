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
/// \file  sw_things.h
/// \brief Sprite rendering for the Software renderer

#ifndef __SW_THINGS__
#define __SW_THINGS__

#include "../r_patch.h"
#include "../r_portal.h"
#include "../r_defs.h"
#include "../r_skins.h"

// ----------
// VISSPRITES
// ----------

// number of sprite lumps for spritewidth,offset,topoffset lookup tables
// Fab: this is a hack : should allocate the lookup tables per sprite
#define MAXVISSPRITES 2048 // added 2-2-98 was 128

#define VISSPRITECHUNKBITS 6	// 2^6 = 64 sprites per chunk
#define VISSPRITESPERCHUNK (1 << VISSPRITECHUNKBITS)
#define VISSPRITEINDEXMASK (VISSPRITESPERCHUNK - 1)

typedef enum
{
	// actual cuts
	SC_NONE = 0,
	SC_TOP = 1,
	SC_BOTTOM = 1<<1,
	// other flags
	SC_PRECIP = 1<<2,
	SC_LINKDRAW = 1<<3,
	SC_FULLBRIGHT = 1<<4,
	SC_VFLIP = 1<<5,
	SC_ISSCALED = 1<<6,
	SC_SHADOW = 1<<7,
	// masks
	SC_CUTMASK = SC_TOP|SC_BOTTOM,
	SC_FLAGMASK = ~SC_CUTMASK
} spritecut_e;

// A vissprite_t is a thing that will be drawn during a refresh,
// i.e. a sprite object that is partly visible.
typedef struct vissprite_s
{
	// Doubly linked list.
	struct vissprite_s *prev;
	struct vissprite_s *next;

	// Bonus linkdraw pointer.
	struct vissprite_s *linkdraw;

	mobj_t *mobj; // for easy access

	INT32 x1, x2;

	fixed_t gx, gy; // for line side calculation
	fixed_t gz, gzt; // global bottom/top for silhouette clipping
	fixed_t pz, pzt; // physical bottom/top for sorting with 3D floors

	fixed_t startfrac; // horizontal position of x1
	fixed_t scale, sortscale; // sortscale only differs from scale for paper sprites and MF2_LINKDRAW
	fixed_t scalestep; // only for paper sprites, 0 otherwise
	fixed_t paperoffset, paperdistance; // for paper sprites, offset/dist relative to the angle
	fixed_t xiscale; // negative if flipped

	angle_t centerangle; // for paper sprites

	struct {
		fixed_t tan; // The amount to shear the sprite vertically per row
		INT32 offset; // The center of the shearing location offset from x1
	} shear;

	fixed_t texturemid;
	patch_t *patch;

	lighttable_t *colormap; // for color translation and shadow draw
	                        // maxbright frames as well

	UINT8 *transmap; // for MF2_SHADOW sprites, which translucency table to use

	INT32 mobjflags;

	INT32 heightsec; // height sector for underwater/fake ceiling support

	extracolormap_t *extra_colormap; // global colormaps

	fixed_t xscale;
	boolean flipped;

	// Precalculated top and bottom screen coords for the sprite.
	fixed_t thingheight; // The actual height of the thing (for 3D floors)
	sector_t *sector; // The sector containing the thing.
	INT16 sz, szt;

	spritecut_e cut;

	INT16 clipbot[MAXVIDWIDTH], cliptop[MAXVIDWIDTH];

	INT32 dispoffset; // copy of info->dispoffset, affects ordering but not drawing

	pixelmap_t *pixelmap;
	void **columnofs;
} vissprite_t;

// ----------------
// SPRITE RENDERING
// ----------------

extern UINT32 visspritecount;

// Constant arrays used for psprite clipping
//  and initializing clipping.
extern INT16 negonearray[MAXVIDWIDTH];
extern INT16 screenheightarray[MAXVIDWIDTH];

//SoM: 6/5/2000: Light sprites correctly!
void SWR_AddSprites(sector_t *sec, INT32 lightlevel);
void SWR_ClearSprites(void);

void SWR_DrawSprite(vissprite_t *spr);
void SWR_DrawPrecipitationSprite(vissprite_t *spr);
void SWR_ClipSprites(drawseg_t* dsstart, portal_t* portal);
void SWR_SortVisSprites(vissprite_t* vsprsortedhead, UINT32 start, UINT32 end);

#endif //__SW_THINGS__
