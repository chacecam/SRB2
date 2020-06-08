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
/// \file  r_plane.h
/// \brief Refresh, visplane stuff (floor, ceilings)

#ifndef __SW_PLANE__
#define __SW_PLANE__

#include "../screen.h" // needs MAXVIDWIDTH/MAXVIDHEIGHT
#include "../r_data.h"
#include "../p_polyobj.h"

#define MAXVISPLANES 512

//
// Now what is a visplane, anyway?
// Simple: kinda floor/ceiling polygon optimised for SRB2 rendering.
//
typedef struct visplane_s
{
	struct visplane_s *next;

	fixed_t height;
	fixed_t viewx, viewy, viewz;
	angle_t viewangle;
	angle_t plangle;
	INT32 picnum;
	INT32 lightlevel;
	INT32 minx, maxx;

	// colormaps per sector
	extracolormap_t *extra_colormap;

	// leave pads for [minx-1]/[maxx+1]
	UINT16 padtopstart, top[MAXVIDWIDTH], padtopend;
	UINT16 padbottomstart, bottom[MAXVIDWIDTH], padbottomend;
	INT32 high, low; // R_PlaneBounds should set these.

	fixed_t xoffs, yoffs; // Scrolling flats.

	struct ffloor_s *ffloor;
	polyobj_t *polyobj;
	pslope_t *slope;
} visplane_t;

extern visplane_t *visplanes[MAXVISPLANES];
extern visplane_t *floorplane;
extern visplane_t *ceilingplane;

// Visplane related.
extern INT16 *lastopening, *openings;
extern size_t maxopenings;

extern INT16 floorclip[MAXVIDWIDTH], ceilingclip[MAXVIDWIDTH];
extern fixed_t frontscale[MAXVIDWIDTH], yslopetab[MAXVIDHEIGHT*16];
extern fixed_t cachedheight[MAXVIDHEIGHT];
extern fixed_t cacheddistance[MAXVIDHEIGHT];
extern fixed_t cachedxstep[MAXVIDHEIGHT];
extern fixed_t cachedystep[MAXVIDHEIGHT];
extern fixed_t basexscale, baseyscale;

extern fixed_t *yslope;
extern lighttable_t **planezlight;

void SWR_InitPlanes(void);
void SWR_ClearPlanes(void);
void SWR_ClearFFloorClips (void);

void SWR_MapPlane(INT32 y, INT32 x1, INT32 x2);
void SWR_MakeSpans(INT32 x, INT32 t1, INT32 b1, INT32 t2, INT32 b2);
void SWR_DrawPlanes(void);
visplane_t *SWR_FindPlane(fixed_t height, INT32 picnum, INT32 lightlevel, fixed_t xoff, fixed_t yoff, angle_t plangle,
	extracolormap_t *planecolormap, ffloor_t *ffloor, polyobj_t *polyobj, pslope_t *slope);
visplane_t *SWR_CheckPlane(visplane_t *pl, INT32 start, INT32 stop);
void SWR_ExpandPlane(visplane_t *pl, INT32 start, INT32 stop);
void SWR_PlaneBounds(visplane_t *plane);

// Draws a single visplane.
void SWR_DrawSinglePlane(visplane_t *pl);
void SWR_CheckFlatLength(size_t size);
boolean SWR_CheckPowersOfTwo(void);

typedef struct planemgr_s
{
	visplane_t *plane;
	fixed_t height;
	fixed_t f_pos; // F for Front sector
	fixed_t b_pos; // B for Back sector
	fixed_t f_frac, f_step;
	fixed_t b_frac, b_step;
	INT16 f_clip[MAXVIDWIDTH];
	INT16 c_clip[MAXVIDWIDTH];

	// For slope rendering; the height at the other end
	fixed_t f_pos_slope;
	fixed_t b_pos_slope;

	struct pslope_s *slope;

	struct ffloor_s *ffloor;
	polyobj_t *polyobj;
} visffloor_t;

extern visffloor_t ffloor[MAXFFLOORS];
extern INT32 numffloors;

void Portal_AddSkyboxPortals (void);
#endif
