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
/// \file  sw_masked.h
/// \brief Masked column drawing

#ifndef __SW_MASKED__
#define __SW_MASKED__

#include "../r_defs.h"

// --------------
// MASKED DRAWING
// --------------
/** Used to count the amount of masked elements
 * per portal to later group them in separate
 * drawnode lists.
 */
typedef struct
{
	size_t drawsegs[2];
	size_t vissprites[2];
	fixed_t viewx, viewy, viewz;			/**< View z stored at the time of the BSP traversal for the view/portal. Masked sorting/drawing needs it. */
	sector_t* viewsector;
} maskcount_t;

void SWR_DrawMasked(maskcount_t* masks, UINT8 nummasks);

#include "sw_things.h"
#include "sw_plane.h"

// ---------------------
// MASKED COLUMN DRAWING
// ---------------------

// vars for SWR_DrawMaskedColumn
extern INT16 *mfloorclip;
extern INT16 *mceilingclip;
extern fixed_t spryscale;
extern fixed_t sprtopscreen;
extern fixed_t sprbotscreen;
extern fixed_t windowtop;
extern fixed_t windowbottom;
extern INT32 lengthcol;

void SWR_DrawMaskedColumn(column_t *column);
void SWR_DrawFlippedMaskedColumn(column_t *column);

// ----------
// DRAW NODES
// ----------

// A drawnode is something that points to a 3D floor, 3D side, or masked
// middle texture. This is used for sorting with sprites.
typedef struct drawnode_s
{
	visplane_t *plane;
	drawseg_t *seg;
	drawseg_t *thickseg;
	ffloor_t *ffloor;
	vissprite_t *sprite;

	struct drawnode_s *next;
	struct drawnode_s *prev;
} drawnode_t;

void SWR_InitDrawNodes(void);

#endif
