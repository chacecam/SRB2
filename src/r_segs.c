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
/// \file  r_segs.c
/// \brief All the clipping: columns, horizontal spans, sky columns

#include "doomdef.h"
#include "r_local.h"
#include "r_sky.h"

#include "r_portal.h"
#include "r_splats.h"

#include "w_wad.h"
#include "z_zone.h"
#include "d_netcmd.h"
#include "m_misc.h"
#include "p_local.h" // Camera...
#include "p_slopes.h"
#include "console.h" // con_clipviewtop

// OPTIMIZE: closed two sided lines as single sided

// True if any of the segs textures might be visible.
static boolean segtextured;
static boolean markfloor; // False if the back side is the same plane.
static boolean markceiling;

static boolean maskedtexture;
static INT32 toptexture, bottomtexture, midtexture;
static INT32 numthicksides, numbackffloors;

angle_t rw_normalangle;
// angle to line origin
angle_t rw_angle1;
float rw_distance;

//
// regular wall
//
static INT32 rw_x, rw_stopx;
static angle_t rw_centerangle;
static float rw_offset;
static float rw_offset2; // for splats
static float rw_scale, rw_scalestep;
static float rw_midtexturemid, rw_toptexturemid, rw_bottomtexturemid;
static float worldtop, worldbottom, worldhigh, worldlow;
static fixed_t worldtopfixed, worldbottomfixed, worldhighfixed, worldlowfixed;
static float worldtopslope, worldbottomslope, worldhighslope, worldlowslope; // worldtop/bottom at end of slope
static fixed_t worldtopslopefixed, worldbottomslopefixed, worldhighslopefixed, worldlowslopefixed; // worldtop/bottom at end of slope
static float rw_toptextureslide, rw_midtextureslide, rw_bottomtextureslide; // Defines how to adjust Y offsets along the wall for slopes
static float rw_midtextureback, rw_midtexturebackslide; // Values for masked midtexture height calculation
static float pixhigh, pixlow, pixhighstep, pixlowstep;
static float topfrac, topstep;
static float bottomfrac, bottomstep;

static lighttable_t **walllights;
static INT16 *maskedtexturecol;
static float *maskedtextureheight = NULL;

// ==========================================================================
// R_Splats Wall Splats Drawer
// ==========================================================================

#ifdef WALLSPLATS
static INT16 last_ceilingclip[MAXVIDWIDTH];
static INT16 last_floorclip[MAXVIDWIDTH];

static void R_DrawSplatColumn(column_t *column)
{
	INT32 topscreen, bottomscreen;
	fixed_t basetexturemid;
	INT32 topdelta, prevdelta = -1;

	basetexturemid = dc_texturemid;

	for (; column->topdelta != 0xff ;)
	{
		// calculate unclipped screen coordinates for post
		topdelta = column->topdelta;
		if (topdelta <= prevdelta)
			topdelta += prevdelta;
		prevdelta = topdelta;
		topscreen = sprtopscreen + spryscale*topdelta;
		bottomscreen = topscreen + spryscale*column->length;

		dc_yl = (topscreen+FRACUNIT-1)>>FRACBITS;
		dc_yh = (bottomscreen-1)>>FRACBITS;

		if (dc_yh >= last_floorclip[dc_x])
			dc_yh = last_floorclip[dc_x] - 1;
		if (dc_yl <= last_ceilingclip[dc_x])
			dc_yl = last_ceilingclip[dc_x] + 1;
		if (dc_yl <= dc_yh && dl_yh < vid.height && yh > 0)
		{
			dc_source = (UINT8 *)column + 3;
			dc_texturemid = basetexturemid - (topdelta<<FRACBITS);

			// Drawn by R_DrawColumn.
			colfunc();
		}
		column = (column_t *)((UINT8 *)column + column->length + 4);
	}

	dc_texturemid = basetexturemid;
}

static void R_DrawWallSplats(void)
{
	wallsplat_t *splat;
	seg_t *seg;
	angle_t angle, angle1, angle2;
	INT32 x1, x2;
	size_t pindex;
	column_t *col;
	patch_t *patch;
	fixed_t texturecolumn;
	float ftexturecolumn;

	splat = (wallsplat_t *)linedef->splats;

	I_Assert(splat != NULL);

	seg = ds_p->curline;

	// draw all splats from the line that touches the range of the seg
	for (; splat; splat = splat->next)
	{
		angle1 = R_PointToAngle(splat->v1.x, splat->v1.y);
		angle2 = R_PointToAngle(splat->v2.x, splat->v2.y);
		angle1 = (angle1 - viewangle + ANGLE_90)>>ANGLETOFINESHIFT;
		angle2 = (angle2 - viewangle + ANGLE_90)>>ANGLETOFINESHIFT;
		// out of the viewangletox lut
		/// \todo clip it to the screen
		if (angle1 > FINEANGLES/2 || angle2 > FINEANGLES/2)
			continue;
		x1 = viewangletox[angle1];
		x2 = viewangletox[angle2];

		if (x1 >= x2)
			continue; // does not cross a pixel

		// splat is not in this seg range
		if (x2 < ds_p->x1 || x1 > ds_p->x2)
			continue;

		if (x1 < ds_p->x1)
			x1 = ds_p->x1;
		if (x2 > ds_p->x2)
			x2 = ds_p->x2;
		if (x2 <= x1)
			continue;

		// calculate incremental stepping values for texture edges
		rw_scalestep = ds_p->scalestep;
		maskedscale = ds_p->scale1 + (x1 - ds_p->x1)*rw_scalestep;
		mfloorclip = floorclip;
		mceilingclip = ceilingclip;

		patch = W_CachePatchNum(splat->patch, PU_PATCH);

		dc_texturemid = splat->top + (SHORT(patch->height)<<(FRACBITS-1)) - viewz;
		if (splat->yoffset)
			dc_texturemid += *splat->yoffset;

		maskedtopscreen = centeryfloat - (FIXED_TO_FLOAT(dc_texturemid)*maskedscale);
		sprtopscreen = FLOAT_TO_FIXED(maskedtopscreen);

		// set drawing mode
		switch (splat->flags & SPLATDRAWMODE_MASK)
		{
			case SPLATDRAWMODE_OPAQUE:
				colfunc = colfuncs[BASEDRAWFUNC];
				break;
			case SPLATDRAWMODE_TRANS:
				if (!cv_translucency.value)
					colfunc = colfuncs[BASEDRAWFUNC];
				else
				{
					dc_transmap = transtables + ((tr_trans50 - 1)<<FF_TRANSSHIFT);
					colfunc = colfuncs[COLDRAWFUNC_FUZZY];
				}

				break;
			case SPLATDRAWMODE_SHADE:
				colfunc = colfuncs[COLDRAWFUNC_SHADE];
				break;
		}

		dc_texheight = 0;

		// draw the columns
		for (dc_x = x1; dc_x <= x2; dc_x++, maskedscale += rw_scalestep)
		{
			pindex = FLOAT_TO_FIXED(maskedscale * LIGHTRESOLUTIONFLOAT)>>LIGHTSCALESHIFT;
			if (pindex >= MAXLIGHTSCALE)
				pindex = MAXLIGHTSCALE - 1;
			dc_colormap = walllights[pindex];

			if (frontsector->extra_colormap)
				dc_colormap = frontsector->extra_colormap->colormap + (dc_colormap - colormaps);

			maskedtopscreen = centeryfloat - FixedMul(FIXED_TO_FLOAT(dc_texturemid)*maskedscale);
			sprtopscreen = FLOAT_TO_FIXED(maskedtopscreen);
			dc_iscale = FLOAT_TO_FIXED(1.0f / maskedscale);

			// find column of patch, from perspective
			angle = (rw_centerangle + xtoviewangle[dc_x])>>ANGLETOFINESHIFT;
			ftexturecolumn = rw_offset2 - FIXED_TO_FLOAT(splat->offset) - (FIXED_TO_FLOAT(FINETANGENT(angle))*rw_distance);

			// FIXME!
			texturecolumn = llrintf(ftexturecolumn);
			if (texturecolumn < 0 || texturecolumn >= SHORT(patch->width))
				continue;

			// draw the texture
			col = (column_t *)((UINT8 *)patch + LONG(patch->columnofs[texturecolumn]));
			spryscale = FLOAT_TO_FIXED(maskedscale);
			R_DrawSplatColumn(col);
		}
	} // next splat

	colfunc = colfuncs[BASEDRAWFUNC];
}

#endif //WALLSPLATS

// ==========================================================================
// R_RenderMaskedSegRange
// ==========================================================================

// If we have a multi-patch texture on a 2sided wall (rare) then we draw
//  it using R_DrawColumn, else we draw it using R_DrawMaskedColumn, this
//  way we don't have to store extra post_t info with each column for
//  multi-patch textures. They are not normally needed as multi-patch
//  textures don't have holes in it. At least not for now.

static void R_DrawMaskedColumnFloat(column_t *column)
{
	float topscreen;
	float bottomscreen;
	fixed_t basetexturemid;
	INT32 topdelta, prevdelta = 0;

	basetexturemid = dc_texturemid;

	for (; column->topdelta != 0xff ;)
	{
		// calculate unclipped screen coordinates
		// for post
		topdelta = column->topdelta;
		if (topdelta <= prevdelta)
			topdelta += prevdelta;
		prevdelta = topdelta;
		topscreen = maskedtopscreen + maskedscale*topdelta;
		bottomscreen = topscreen + maskedscale*column->length;

		dc_yl = (INT32)llrintf(topscreen+0.5f);
		dc_yh = (INT32)llrintf(bottomscreen-0.5f);

		if (windowtop != INT32_MAX && windowbottom != INT32_MAX)
		{
			if (windowtop > FLOAT_TO_FIXED(topscreen))
				dc_yl = (windowtop + FRACUNIT - 1)>>FRACBITS;
			if (windowbottom < FLOAT_TO_FIXED(bottomscreen))
				dc_yh = (windowbottom - 1)>>FRACBITS;
		}

		if (dc_yh >= mfloorclip[dc_x])
			dc_yh = mfloorclip[dc_x]-1;
		if (dc_yl <= mceilingclip[dc_x])
			dc_yl = mceilingclip[dc_x]+1;
		if (dc_yl < 0)
			dc_yl = 0;
		if (dc_yh >= vid.height)
			dc_yh = vid.height - 1;

		if (dc_yl <= dc_yh && dc_yl < vid.height && dc_yh > 0)
		{
			dc_source = (UINT8 *)column + 3;
			dc_texturemid = basetexturemid - (topdelta<<FRACBITS);

			// Drawn by R_DrawColumn.
			// This stuff is a likely cause of the splitscreen water crash bug.
			// FIXTHIS: Figure out what "something more proper" is and do it.
			// quick fix... something more proper should be done!!!
			if (ylookup[dc_yl])
				colfunc();
			else if (colfunc == R_DrawColumn_8
#ifdef USEASM
			|| colfunc == R_DrawColumn_8_ASM || colfunc == R_DrawColumn_8_MMX
#endif
			)
			{
				static INT32 first = 1;
				if (first)
				{
					CONS_Debug(DBG_RENDER, "WARNING: avoiding a crash in %s %d\n", __FILE__, __LINE__);
					first = 0;
				}
			}
		}
		column = (column_t *)((UINT8 *)column + column->length + 4);
	}

	dc_texturemid = basetexturemid;
}

static void R_DrawFlippedMaskedColumnFloat(column_t *column)
{
	float topscreen;
	float bottomscreen;
	fixed_t basetexturemid = dc_texturemid;
	INT32 topdelta, prevdelta = -1;
	UINT8 *d,*s;

	for (; column->topdelta != 0xff ;)
	{
		// calculate unclipped screen coordinates
		// for post
		topdelta = column->topdelta;
		if (topdelta <= prevdelta)
			topdelta += prevdelta;
		prevdelta = topdelta;
		topdelta = lengthcol-column->length-topdelta;
		topscreen = maskedtopscreen + maskedscale*topdelta;
		bottomscreen = maskedbotscreen + maskedscale*column->length;

		dc_yl = (INT32)llrintf(topscreen+0.5f);
		dc_yh = (INT32)llrintf(bottomscreen-0.5f);

		if (windowtop != INT32_MAX && windowbottom != INT32_MAX)
		{
			if (windowtop > topscreen)
				dc_yl = (windowtop + FRACUNIT - 1)>>FRACBITS;
			if (windowbottom < bottomscreen)
				dc_yh = (windowbottom - 1)>>FRACBITS;
		}

		if (dc_yh >= mfloorclip[dc_x])
			dc_yh = mfloorclip[dc_x]-1;
		if (dc_yl <= mceilingclip[dc_x])
			dc_yl = mceilingclip[dc_x]+1;
		if (dc_yl < 0)
			dc_yl = 0;
		if (dc_yh >= vid.height) // dc_yl must be < vid.height, so reduces number of checks in tight loop
			dc_yh = vid.height - 1;

		if (dc_yl <= dc_yh && dc_yh > 0)
		{
			dc_source = ZZ_Alloc(column->length);
			for (s = (UINT8 *)column+2+column->length, d = dc_source; d < dc_source+column->length; --s)
				*d++ = *s;
			dc_texturemid = basetexturemid - (topdelta<<FRACBITS);

			// Still drawn by R_DrawColumn.
			if (ylookup[dc_yl])
				colfunc();
#ifdef PARANOIA
			else
				I_Error("R_DrawMaskedColumn: Invalid ylookup for dc_yl %d", dc_yl);
#endif
			Z_Free(dc_source);
		}
		column = (column_t *)((UINT8 *)column + column->length + 4);
	}

	dc_texturemid = basetexturemid;
}

static void R_Render2sidedMultiPatchColumn(column_t *column)
{
	float topscreen, bottomscreen;

	topscreen = maskedtopscreen; // + spryscale*column->topdelta;  topdelta is 0 for the wall
	bottomscreen = topscreen + maskedscale * lengthcol;

	dc_yl = (INT32)llrintf(sprtopscreen+0.5f);
	dc_yh = (INT32)llrintf(bottomscreen-0.5f);

	if (windowtop != INT32_MAX && windowbottom != INT32_MAX)
	{
		dc_yl = ((windowtop + FRACUNIT)>>FRACBITS);
		dc_yh = (windowbottom - 1)>>FRACBITS;
	}

	if (dc_yh >= mfloorclip[dc_x])
		dc_yh =  mfloorclip[dc_x] - 1;
	if (dc_yl <= mceilingclip[dc_x])
		dc_yl =  mceilingclip[dc_x] + 1;

	if (dc_yl >= vid.height || dc_yh < 0)
		return;

	if (dc_yl <= dc_yh && dc_yh < vid.height && dc_yh > 0)
	{
		dc_source = (UINT8 *)column + 3;

		if (colfunc == colfuncs[BASEDRAWFUNC])
			(colfuncs[COLDRAWFUNC_TWOSMULTIPATCH])();
		else if (colfunc == colfuncs[COLDRAWFUNC_FUZZY])
			(colfuncs[COLDRAWFUNC_TWOSMULTIPATCHTRANS])();
		else
			colfunc();
	}
}

void R_RenderMaskedSegRange(drawseg_t *ds, INT32 x1, INT32 x2)
{
	size_t pindex;
	column_t *col;
	INT32 lightnum, texnum, i;
	fixed_t height, realbot;
	lightlist_t *light;
	r_lightlist_t *rlight;
	void (*colfunc_2s)(column_t *);
	line_t *ldef;
	sector_t *front, *back;
	INT32 times, repeats;
	INT64 overflow_test;
	INT32 range;

	// Calculate light table.
	// Use different light tables
	//   for horizontal / vertical / diagonal. Diagonal?
	// OPTIMIZE: get rid of LIGHTSEGSHIFT globally
	curline = ds->curline;

	frontsector = curline->frontsector;
	backsector = curline->backsector;
	texnum = R_GetTextureNum(curline->sidedef->midtexture);
	windowbottom = windowtop = sprbotscreen = INT32_MAX;

	// hack translucent linedef types (900-909 for transtables 1-9)
	ldef = curline->linedef;
	switch (ldef->special)
	{
		case 900:
		case 901:
		case 902:
		case 903:
		case 904:
		case 905:
		case 906:
		case 907:
		case 908:
			dc_transmap = transtables + ((ldef->special-900)<<FF_TRANSSHIFT);
			colfunc = colfuncs[COLDRAWFUNC_FUZZY];
			break;
		case 909:
			colfunc = colfuncs[COLDRAWFUNC_FOG];
			windowtop = frontsector->ceilingheight;
			windowbottom = frontsector->floorheight;
			break;
		default:
			colfunc = colfuncs[BASEDRAWFUNC];
			break;
	}

	if (curline->polyseg && curline->polyseg->translucency > 0)
	{
		if (curline->polyseg->translucency >= NUMTRANSMAPS)
			return;

		dc_transmap = transtables + ((curline->polyseg->translucency-1)<<FF_TRANSSHIFT);
		colfunc = colfuncs[COLDRAWFUNC_FUZZY];
	}

	range = max(ds->x2-ds->x1, 1);
	rw_scalestep = ds->scalestep;
	maskedscale = ds->scale1 + (x1 - ds->x1)*rw_scalestep;

	// Texture must be cached before setting colfunc_2s,
	// otherwise texture[texnum]->holes may be false when it shouldn't be
	R_CheckTextureCache(texnum);
	// handle case where multipatch texture is drawn on a 2sided wall, multi-patch textures
	// are not stored per-column with post info in SRB2
	if (textures[texnum]->holes)
	{
		if (textures[texnum]->flip & 2) // vertically flipped?
		{
			colfunc_2s = R_DrawFlippedMaskedColumnFloat;
			lengthcol = textures[texnum]->height;
		}
		else
			colfunc_2s = R_DrawMaskedColumnFloat; // render the usual 2sided single-patch packed texture
	}
	else
	{
		colfunc_2s = R_Render2sidedMultiPatchColumn; // render multipatch with no holes (no post_t info)
		lengthcol = textures[texnum]->height;
	}

	// Setup lighting based on the presence/lack-of 3D floors.
	dc_numlights = 0;
	if (frontsector->numlights)
	{
		dc_numlights = frontsector->numlights;
		if (dc_numlights >= dc_maxlights)
		{
			dc_maxlights = dc_numlights;
			dc_lightlist = Z_Realloc(dc_lightlist, sizeof (*dc_lightlist) * dc_maxlights, PU_STATIC, NULL);
		}

		for (i = 0; i < dc_numlights; i++)
		{
			float leftheight, rightheight;
			light = &frontsector->lightlist[i];
			rlight = &dc_lightlist[i];
			if (light->slope) {
				fixed_t left = P_GetZAt(light->slope, ds->leftpos.x, ds->leftpos.y);
				fixed_t right = P_GetZAt(light->slope, ds->rightpos.x, ds->rightpos.y);
				leftheight = FIXED_TO_FLOAT(left);
				rightheight = FIXED_TO_FLOAT(right);
			} else
				leftheight = rightheight = FIXED_TO_FLOAT(light->height);

			leftheight -= FIXED_TO_FLOAT(viewz);
			rightheight -= FIXED_TO_FLOAT(viewz);

			rlight->height = FLOAT_TO_FIXED(centeryfloat - (leftheight*ds->scale1));
			rlight->heightstep = FLOAT_TO_FIXED(centeryfloat - (rightheight*ds->scale2));
			rlight->heightstep = (rlight->heightstep-rlight->height)/(range);
			//if (x1 > ds->x1)
				//rlight->height -= (x1 - ds->x1)*rlight->heightstep;
			rlight->startheight = rlight->height; // keep starting value here to reset for each repeat
			rlight->lightlevel = *light->lightlevel;
			rlight->extra_colormap = *light->extra_colormap;
			rlight->flags = light->flags;

			if ((colfunc != colfuncs[COLDRAWFUNC_FUZZY])
				|| (rlight->flags & FF_FOG)
				|| (rlight->extra_colormap && (rlight->extra_colormap->flags & CMF_FOG)))
				lightnum = (rlight->lightlevel >> LIGHTSEGSHIFT);
			else
				lightnum = LIGHTLEVELS - 1;

			if (rlight->extra_colormap && (rlight->extra_colormap->flags & CMF_FOG))
				;
			else if (curline->v1->y == curline->v2->y)
				lightnum--;
			else if (curline->v1->x == curline->v2->x)
				lightnum++;

			rlight->lightnum = lightnum;
		}
	}
	else
	{
		if ((colfunc != colfuncs[COLDRAWFUNC_FUZZY])
			|| (frontsector->extra_colormap && (frontsector->extra_colormap->flags & CMF_FOG)))
			lightnum = (frontsector->lightlevel >> LIGHTSEGSHIFT);
		else
			lightnum = LIGHTLEVELS - 1;

		if (colfunc == colfuncs[COLDRAWFUNC_FOG]
			|| (frontsector->extra_colormap && (frontsector->extra_colormap->flags & CMF_FOG)))
			;
		else if (curline->v1->y == curline->v2->y)
			lightnum--;
		else if (curline->v1->x == curline->v2->x)
			lightnum++;

		if (lightnum < 0)
			walllights = scalelight[0];
		else if (lightnum >= LIGHTLEVELS)
			walllights = scalelight[LIGHTLEVELS - 1];
		else
			walllights = scalelight[lightnum];
	}

	maskedtexturecol = ds->maskedtexturecol;

	mfloorclip = ds->sprbottomclip;
	mceilingclip = ds->sprtopclip;

	if (frontsector->heightsec != -1)
		front = &sectors[frontsector->heightsec];
	else
		front = frontsector;

	if (backsector->heightsec != -1)
		back = &sectors[backsector->heightsec];
	else
		back = backsector;

	if (ds->curline->sidedef->repeatcnt)
		repeats = 1 + ds->curline->sidedef->repeatcnt;
	else if (ldef->flags & ML_EFFECT5)
	{
		fixed_t high, low;

		if (front->ceilingheight > back->ceilingheight)
			high = back->ceilingheight;
		else
			high = front->ceilingheight;

		if (front->floorheight > back->floorheight)
			low = front->floorheight;
		else
			low = back->floorheight;

		repeats = (high - low)/textureheight[texnum];
		if ((high-low)%textureheight[texnum])
			repeats++; // tile an extra time to fill the gap -- Monster Iestyn
	}
	else
		repeats = 1;

	for (times = 0; times < repeats; times++)
	{
		if (times > 0)
		{
			rw_scalestep = ds->scalestep;
			maskedscale = ds->scale1 + (x1 - ds->x1)*rw_scalestep;
			if (dc_numlights)
			{ // reset all lights to their starting heights
				for (i = 0; i < dc_numlights; i++)
				{
					rlight = &dc_lightlist[i];
					rlight->height = rlight->startheight;
				}
			}
		}

		dc_texheight = textureheight[texnum]>>FRACBITS;

		// draw the columns
		for (dc_x = x1; dc_x <= x2; dc_x++)
		{
			dc_texturemid = FLOAT_TO_FIXED(ds->maskedtextureheight[dc_x]);

			if (!!(curline->linedef->flags & ML_DONTPEGBOTTOM) ^ !!(curline->linedef->flags & ML_EFFECT3))
				dc_texturemid += (textureheight[texnum])*times + textureheight[texnum];
			else
				dc_texturemid -= (textureheight[texnum])*times;
			// calculate lighting
			if (maskedtexturecol[dc_x] != INT16_MAX)
			{
				// Check for overflows first
				overflow_test = (INT64)centeryfrac - (((INT64)dc_texturemid*FLOAT_TO_FIXED(maskedscale))>>FRACBITS);
				if (overflow_test < 0) overflow_test = -overflow_test;
				if ((UINT64)overflow_test&0xFFFFFFFF80000000ULL)
				{
					// Eh, no, go away, don't waste our time
					if (dc_numlights)
					{
						for (i = 0; i < dc_numlights; i++)
						{
							rlight = &dc_lightlist[i];
							rlight->height += rlight->heightstep;
						}
					}
					maskedscale += rw_scalestep;
					continue;
				}

				if (dc_numlights)
				{
					lighttable_t **xwalllights;

					sprbotscreen = INT32_MAX;
					maskedtopscreen = centeryfloat - (FIXED_TO_FLOAT(dc_texturemid)*maskedscale);
					sprtopscreen = windowtop = FLOAT_TO_FIXED(maskedtopscreen);

					realbot = windowbottom = FixedMul(textureheight[texnum], FLOAT_TO_FIXED(maskedscale)) + sprtopscreen;
					dc_iscale = FLOAT_TO_FIXED(1.0f / maskedscale);

					// draw the texture
					col = (column_t *)((UINT8 *)R_GetColumn(texnum, maskedtexturecol[dc_x]) - 3);
					spryscale = FLOAT_TO_FIXED(maskedscale);

					for (i = 0; i < dc_numlights; i++)
					{
						rlight = &dc_lightlist[i];

						if ((rlight->flags & FF_NOSHADE))
							continue;

						if (rlight->lightnum < 0)
							xwalllights = scalelight[0];
						else if (rlight->lightnum >= LIGHTLEVELS)
							xwalllights = scalelight[LIGHTLEVELS-1];
						else
							xwalllights = scalelight[rlight->lightnum];

						pindex = FLOAT_TO_FIXED(maskedscale * LIGHTRESOLUTIONFLOAT)>>LIGHTSCALESHIFT;

						if (pindex >= MAXLIGHTSCALE)
							pindex = MAXLIGHTSCALE - 1;

						if (rlight->extra_colormap)
							rlight->rcolormap = rlight->extra_colormap->colormap + (xwalllights[pindex] - colormaps);
						else
							rlight->rcolormap = xwalllights[pindex];

						height = rlight->height;
						rlight->height += rlight->heightstep;

						if (height <= windowtop)
						{
							dc_colormap = rlight->rcolormap;
							continue;
						}

						windowbottom = height;
						if (windowbottom >= realbot)
						{
							windowbottom = realbot;
							colfunc_2s(col);
							for (i++; i < dc_numlights; i++)
							{
								rlight = &dc_lightlist[i];
								rlight->height += rlight->heightstep;
							}

							continue;
						}
						colfunc_2s(col);
						windowtop = windowbottom + 1;
						dc_colormap = rlight->rcolormap;
					}
					windowbottom = realbot;
					if (windowtop < windowbottom)
						colfunc_2s(col);

					maskedscale += rw_scalestep;
					continue;
				}

				// calculate lighting
				pindex = FLOAT_TO_FIXED(maskedscale * LIGHTRESOLUTIONFLOAT)>>LIGHTSCALESHIFT;

				if (pindex >= MAXLIGHTSCALE)
					pindex = MAXLIGHTSCALE - 1;

				dc_colormap = walllights[pindex];

				if (frontsector->extra_colormap)
					dc_colormap = frontsector->extra_colormap->colormap + (dc_colormap - colormaps);

				maskedtopscreen = centeryfloat - (FIXED_TO_FLOAT(dc_texturemid)*maskedscale);
				sprtopscreen = FLOAT_TO_FIXED(maskedtopscreen);
				dc_iscale = FLOAT_TO_FIXED(1.0f / maskedscale);

				// draw the texture
				col = (column_t *)((UINT8 *)R_GetColumn(texnum, maskedtexturecol[dc_x]) - 3);
				spryscale = FLOAT_TO_FIXED(maskedscale);

#if 0 // Disabling this allows inside edges to render below the planes, for until the clipping is fixed to work right when POs are near the camera. -Red
				if (curline->dontrenderme && curline->polyseg && (curline->polyseg->flags & POF_RENDERPLANES))
				{
					fixed_t my_topscreen;
					fixed_t my_bottomscreen;
					fixed_t my_yl, my_yh;

					my_topscreen = sprtopscreen + spryscale*col->topdelta;
					my_bottomscreen = sprbotscreen == INT32_MAX ? my_topscreen + spryscale*col->length
					                                         : sprbotscreen + spryscale*col->length;

					my_yl = (my_topscreen+FRACUNIT-1)>>FRACBITS;
					my_yh = (my_bottomscreen-1)>>FRACBITS;
	//				CONS_Debug(DBG_RENDER, "my_topscreen: %d\nmy_bottomscreen: %d\nmy_yl: %d\nmy_yh: %d\n", my_topscreen, my_bottomscreen, my_yl, my_yh);

					if (numffloors)
					{
						INT32 top = my_yl;
						INT32 bottom = my_yh;

						for (i = 0; i < numffloors; i++)
						{
							if (!ffloor[i].polyobj || ffloor[i].polyobj != curline->polyseg)
								continue;

							if (ffloor[i].height < viewz)
							{
								INT32 top_w = ffloor[i].plane->top[dc_x];

	//							CONS_Debug(DBG_RENDER, "Leveltime : %d\n", leveltime);
	//							CONS_Debug(DBG_RENDER, "Top is %d, top_w is %d\n", top, top_w);
								if (top_w < top)
								{
									ffloor[i].plane->top[dc_x] = (INT16)top;
									ffloor[i].plane->picnum = 0;
								}
	//							CONS_Debug(DBG_RENDER, "top_w is now %d\n", ffloor[i].plane->top[dc_x]);
							}
							else if (ffloor[i].height > viewz)
							{
								INT32 bottom_w = ffloor[i].plane->bottom[dc_x];

								if (bottom_w > bottom)
								{
									ffloor[i].plane->bottom[dc_x] = (INT16)bottom;
									ffloor[i].plane->picnum = 0;
								}
							}
						}
					}
				}
				else
#endif
					colfunc_2s(col);
			}
			maskedscale += rw_scalestep;
		}
	}
	colfunc = colfuncs[BASEDRAWFUNC];
}

// Loop through R_DrawMaskedColumnFloat calls
static void R_DrawRepeatMaskedColumn(column_t *col)
{
	while (sprtopscreen < sprbotscreen) {
		maskedtopscreen = FIXED_TO_FLOAT(sprtopscreen);
		maskedbotscreen = FIXED_TO_FLOAT(sprbotscreen);
		R_DrawMaskedColumnFloat(col);
		if ((INT64)sprtopscreen + FLOAT_TO_FIXED(dc_texheight*maskedscale) > (INT64)INT32_MAX) // prevent overflow
			sprtopscreen = INT32_MAX;
		else
			sprtopscreen += FLOAT_TO_FIXED(dc_texheight*maskedscale);
	}
}

static void R_DrawRepeatFlippedMaskedColumn(column_t *col)
{
	do {
		R_DrawFlippedMaskedColumn(col);
		sprtopscreen += dc_texheight*spryscale;
	} while (sprtopscreen < sprbotscreen);
}

//
// R_RenderThickSideRange
// Renders all the thick sides in the given range.
void R_RenderThickSideRange(drawseg_t *ds, INT32 x1, INT32 x2, ffloor_t *pfloor)
{
	size_t          pindex;
	column_t *      col;
	INT32             lightnum;
	INT32            texnum;
	sector_t        tempsec;
	INT32             templight;
	INT32             i, p;
	fixed_t         bottombounds = viewheight << FRACBITS;
	fixed_t         topbounds = (con_clipviewtop - 1) << FRACBITS;
	fixed_t         offsetvalue = 0;
	lightlist_t     *light;
	r_lightlist_t   *rlight;
	INT32           range;
	line_t          *newline = NULL;
	// Render FOF sides kinda like normal sides, with the frac and step and everything
	// NOTE: INT64 instead of fixed_t because overflow concerns
	INT64         top_frac, top_step, bottom_frac, bottom_step;
	// skew FOF walls with slopes?
	boolean	      slopeskew = false;
	fixed_t       ffloortextureslide = 0;
	INT32         oldx = -1;
	fixed_t       left_top, left_bottom; // needed here for slope skewing
	pslope_t      *skewslope = NULL;

	void (*colfunc_2s) (column_t *);

	// Calculate light table.
	// Use different light tables
	//   for horizontal / vertical / diagonal. Diagonal?
	// OPTIMIZE: get rid of LIGHTSEGSHIFT globally

	curline = ds->curline;
	backsector = pfloor->target;
	frontsector = curline->frontsector == pfloor->target ? curline->backsector : curline->frontsector;
	texnum = R_GetTextureNum(sides[pfloor->master->sidenum[0]].midtexture);

	colfunc = colfuncs[BASEDRAWFUNC];

	if (pfloor->master->flags & ML_TFERLINE)
	{
		size_t linenum = curline->linedef-backsector->lines[0];
		newline = pfloor->master->frontsector->lines[0] + linenum;
		texnum = R_GetTextureNum(sides[newline->sidenum[0]].midtexture);
	}

	if (pfloor->flags & FF_TRANSLUCENT)
	{
		boolean fuzzy = true;

		// Hacked up support for alpha value in software mode Tails 09-24-2002
		if (pfloor->alpha < 12)
			return; // Don't even draw it
		else if (pfloor->alpha < 38)
			dc_transmap = transtables + ((tr_trans90-1)<<FF_TRANSSHIFT);
		else if (pfloor->alpha < 64)
			dc_transmap = transtables + ((tr_trans80-1)<<FF_TRANSSHIFT);
		else if (pfloor->alpha < 89)
			dc_transmap = transtables + ((tr_trans70-1)<<FF_TRANSSHIFT);
		else if (pfloor->alpha < 115)
			dc_transmap = transtables + ((tr_trans60-1)<<FF_TRANSSHIFT);
		else if (pfloor->alpha < 140)
			dc_transmap = transtables + ((tr_trans50-1)<<FF_TRANSSHIFT);
		else if (pfloor->alpha < 166)
			dc_transmap = transtables + ((tr_trans40-1)<<FF_TRANSSHIFT);
		else if (pfloor->alpha < 192)
			dc_transmap = transtables + ((tr_trans30-1)<<FF_TRANSSHIFT);
		else if (pfloor->alpha < 217)
			dc_transmap = transtables + ((tr_trans20-1)<<FF_TRANSSHIFT);
		else if (pfloor->alpha < 243)
			dc_transmap = transtables + ((tr_trans10-1)<<FF_TRANSSHIFT);
		else
			fuzzy = false; // Opaque

		if (fuzzy)
			colfunc = colfuncs[COLDRAWFUNC_FUZZY];
	}
	else if (pfloor->flags & FF_FOG)
		colfunc = colfuncs[COLDRAWFUNC_FOG];

	range = max(ds->x2-ds->x1, 1);
	//SoM: Moved these up here so they are available for my lightlist calculations
	rw_scalestep = ds->scalestep;
	maskedscale = ds->scale1 + (x1 - ds->x1)*rw_scalestep;

	dc_numlights = 0;
	if (frontsector->numlights)
	{
		dc_numlights = frontsector->numlights;
		if (dc_numlights > dc_maxlights)
		{
			dc_maxlights = dc_numlights;
			dc_lightlist = Z_Realloc(dc_lightlist, sizeof (*dc_lightlist) * dc_maxlights, PU_STATIC, NULL);
		}

		for (i = p = 0; i < dc_numlights; i++)
		{
			fixed_t leftheight, rightheight;
			fixed_t pfloorleft, pfloorright;
			INT64 overflow_test;
			light = &frontsector->lightlist[i];
			rlight = &dc_lightlist[p];

#define SLOPEPARAMS(slope, end1, end2, normalheight) \
	if (slope) { \
		end1 = P_GetZAt(slope, ds->leftpos.x, ds->leftpos.y); \
		end2 = P_GetZAt(slope, ds->rightpos.x, ds->rightpos.y); \
	} else \
		end1 = end2 = normalheight;

			SLOPEPARAMS(light->slope,     leftheight, rightheight, light->height)
			SLOPEPARAMS(*pfloor->b_slope, pfloorleft, pfloorright, *pfloor->bottomheight)

			if (leftheight < pfloorleft && rightheight < pfloorright)
				continue;

			SLOPEPARAMS(*pfloor->t_slope, pfloorleft, pfloorright, *pfloor->topheight)

			if (leftheight > pfloorleft && rightheight > pfloorright && i+1 < dc_numlights)
			{
				lightlist_t *nextlight = &frontsector->lightlist[i+1];
				if ((nextlight->slope ? P_GetZAt(nextlight->slope, ds->leftpos.x, ds->leftpos.y) : nextlight->height) > pfloorleft
				 && (nextlight->slope ? P_GetZAt(nextlight->slope, ds->rightpos.x, ds->rightpos.y) : nextlight->height) > pfloorright)
					continue;
			}

			leftheight -= viewz;
			rightheight -= viewz;

#define CLAMPMAX INT32_MAX
#define CLAMPMIN (-INT32_MAX) // This is not INT32_MIN on purpose! INT32_MIN makes the drawers freak out.
			// Monster Iestyn (25/03/18): do not skip these lights if they fail overflow test, just clamp them instead so they behave.
			overflow_test = (INT64)centeryfrac - (((INT64)leftheight*FLOAT_TO_FIXED(ds->scale1))>>FRACBITS);
			if      (overflow_test > (INT64)CLAMPMAX) rlight->height = CLAMPMAX;
			else if (overflow_test > (INT64)CLAMPMIN) rlight->height = (fixed_t)overflow_test;
			else                                      rlight->height = CLAMPMIN;

			overflow_test = (INT64)centeryfrac - (((INT64)rightheight*FLOAT_TO_FIXED(ds->scale2))>>FRACBITS);
			if      (overflow_test > (INT64)CLAMPMAX) rlight->heightstep = CLAMPMAX;
			else if (overflow_test > (INT64)CLAMPMIN) rlight->heightstep = (fixed_t)overflow_test;
			else                                      rlight->heightstep = CLAMPMIN;
			rlight->heightstep = (rlight->heightstep-rlight->height)/(range);
			rlight->flags = light->flags;
			if (light->flags & FF_CUTLEVEL)
			{
				SLOPEPARAMS(*light->caster->b_slope, leftheight, rightheight, *light->caster->bottomheight)
#undef SLOPEPARAMS
				leftheight -= viewz;
				rightheight -= viewz;

				// Monster Iestyn (25/03/18): do not skip these lights if they fail overflow test, just clamp them instead so they behave.
				overflow_test = (INT64)centeryfrac - (((INT64)leftheight*FLOAT_TO_FIXED(ds->scale1))>>FRACBITS);
				if      (overflow_test > (INT64)CLAMPMAX) rlight->botheight = CLAMPMAX;
				else if (overflow_test > (INT64)CLAMPMIN) rlight->botheight = (fixed_t)overflow_test;
				else                                      rlight->botheight = CLAMPMIN;

				overflow_test = (INT64)centeryfrac - (((INT64)rightheight*FLOAT_TO_FIXED(ds->scale2))>>FRACBITS);
				if      (overflow_test > (INT64)CLAMPMAX) rlight->botheightstep = CLAMPMAX;
				else if (overflow_test > (INT64)CLAMPMIN) rlight->botheightstep = (fixed_t)overflow_test;
				else                                      rlight->botheightstep = CLAMPMIN;
				rlight->botheightstep = (rlight->botheightstep-rlight->botheight)/(range);
			}

			rlight->lightlevel = *light->lightlevel;
			rlight->extra_colormap = *light->extra_colormap;

			// Check if the current light effects the colormap/lightlevel
			if (pfloor->flags & FF_FOG)
				rlight->lightnum = (pfloor->master->frontsector->lightlevel >> LIGHTSEGSHIFT);
			else
				rlight->lightnum = (rlight->lightlevel >> LIGHTSEGSHIFT);

			if (pfloor->flags & FF_FOG || rlight->flags & FF_FOG || (rlight->extra_colormap && (rlight->extra_colormap->flags & CMF_FOG)))
				;
			else if (curline->v1->y == curline->v2->y)
				rlight->lightnum--;
			else if (curline->v1->x == curline->v2->x)
				rlight->lightnum++;

			p++;
		}

		dc_numlights = p;
	}
	else
	{
		// Get correct light level!
		if ((frontsector->extra_colormap && (frontsector->extra_colormap->flags & CMF_FOG)))
			lightnum = (frontsector->lightlevel >> LIGHTSEGSHIFT);
		else if (pfloor->flags & FF_FOG)
			lightnum = (pfloor->master->frontsector->lightlevel >> LIGHTSEGSHIFT);
		else if (colfunc == colfuncs[COLDRAWFUNC_FUZZY])
			lightnum = LIGHTLEVELS-1;
		else
			lightnum = R_FakeFlat(frontsector, &tempsec, &templight, &templight, false)
				->lightlevel >> LIGHTSEGSHIFT;

		if (pfloor->flags & FF_FOG || (frontsector->extra_colormap && (frontsector->extra_colormap->flags & CMF_FOG)));
			else if (curline->v1->y == curline->v2->y)
		lightnum--;
		else if (curline->v1->x == curline->v2->x)
			lightnum++;

		if (lightnum < 0)
			walllights = scalelight[0];
		else if (lightnum >= LIGHTLEVELS)
			walllights = scalelight[LIGHTLEVELS-1];
		else
			walllights = scalelight[lightnum];
	}

	maskedtexturecol = ds->thicksidecol;

	mfloorclip = ds->sprbottomclip;
	mceilingclip = ds->sprtopclip;
	dc_texheight = textureheight[texnum]>>FRACBITS;

	// calculate both left ends
	if (*pfloor->t_slope)
		left_top = P_GetZAt(*pfloor->t_slope, ds->leftpos.x, ds->leftpos.y) - viewz;
	else
		left_top = *pfloor->topheight - viewz;

	if (*pfloor->b_slope)
		left_bottom = P_GetZAt(*pfloor->b_slope, ds->leftpos.x, ds->leftpos.y) - viewz;
	else
		left_bottom = *pfloor->bottomheight - viewz;
	skewslope = *pfloor->t_slope; // skew using top slope by default
	if (newline)
	{
		if (newline->flags & ML_DONTPEGTOP)
			slopeskew = true;
	}
	else if (pfloor->master->flags & ML_DONTPEGTOP)
		slopeskew = true;

	if (slopeskew)
		dc_texturemid = left_top;
	else
		dc_texturemid = *pfloor->topheight - viewz;

	if (newline)
	{
		offsetvalue = sides[newline->sidenum[0]].rowoffset;
		if (newline->flags & ML_DONTPEGBOTTOM)
		{
			skewslope = *pfloor->b_slope; // skew using bottom slope
			if (slopeskew)
				dc_texturemid = left_bottom;
			else
				offsetvalue -= *pfloor->topheight - *pfloor->bottomheight;
		}
	}
	else
	{
		offsetvalue = sides[pfloor->master->sidenum[0]].rowoffset;
		if (curline->linedef->flags & ML_DONTPEGBOTTOM)
		{
			skewslope = *pfloor->b_slope; // skew using bottom slope
			if (slopeskew)
				dc_texturemid = left_bottom;
			else
				offsetvalue -= *pfloor->topheight - *pfloor->bottomheight;
		}
	}

	if (slopeskew)
	{
		angle_t lineangle = R_PointToAngle2(curline->v1->x, curline->v1->y, curline->v2->x, curline->v2->y);

		if (skewslope)
			ffloortextureslide = FixedMul(skewslope->zdelta, FINECOSINE((lineangle-skewslope->xydirection)>>ANGLETOFINESHIFT));
	}

	dc_texturemid += offsetvalue;

	// Texture must be cached before setting colfunc_2s,
	// otherwise texture[texnum]->holes may be false when it shouldn't be
	R_CheckTextureCache(texnum);
	//faB: handle case where multipatch texture is drawn on a 2sided wall, multi-patch textures
	//     are not stored per-column with post info anymore in Doom Legacy
	if (textures[texnum]->holes)
	{
		if (textures[texnum]->flip & 2) // vertically flipped?
		{
			colfunc_2s = R_DrawRepeatFlippedMaskedColumn;
			lengthcol = textures[texnum]->height;
		}
		else
			colfunc_2s = R_DrawRepeatMaskedColumn; // render the usual 2sided single-patch packed texture
	}
	else
	{
		colfunc_2s = R_Render2sidedMultiPatchColumn;        //render multipatch with no holes (no post_t info)
		lengthcol = textures[texnum]->height;
	}

	// Set heights according to plane, or slope, whichever
	{
		fixed_t right_top, right_bottom;

		// calculate right ends now
		if (*pfloor->t_slope)
			right_top = P_GetZAt(*pfloor->t_slope, ds->rightpos.x, ds->rightpos.y) - viewz;
		else
			right_top = *pfloor->topheight - viewz;

		if (*pfloor->b_slope)
			right_bottom = P_GetZAt(*pfloor->b_slope, ds->rightpos.x, ds->rightpos.y) - viewz;
		else
			right_bottom = *pfloor->bottomheight - viewz;

		// using INT64 to avoid 32bit overflow
		top_frac =    (INT64)centeryfrac - (((INT64)left_top     * FLOAT_TO_FIXED(ds->scale1)) >> FRACBITS);
		bottom_frac = (INT64)centeryfrac - (((INT64)left_bottom  * FLOAT_TO_FIXED(ds->scale1)) >> FRACBITS);
		top_step =    (INT64)centeryfrac - (((INT64)right_top    * FLOAT_TO_FIXED(ds->scale2)) >> FRACBITS);
		bottom_step = (INT64)centeryfrac - (((INT64)right_bottom * FLOAT_TO_FIXED(ds->scale2)) >> FRACBITS);

		top_step = (top_step-top_frac)/(range);
		bottom_step = (bottom_step-bottom_frac)/(range);

		top_frac += top_step * (x1 - ds->x1);
		bottom_frac += bottom_step * (x1 - ds->x1);
	}

	// draw the columns
	for (dc_x = x1; dc_x <= x2; dc_x++)
	{
		if (maskedtexturecol[dc_x] != INT16_MAX)
		{
			if (ffloortextureslide) { // skew FOF walls
				if (oldx != -1)
					dc_texturemid += FixedMul(ffloortextureslide, (maskedtexturecol[oldx]-maskedtexturecol[dc_x])<<FRACBITS);
				oldx = dc_x;
			}
			// Calculate bounds
			// clamp the values if necessary to avoid overflows and rendering glitches caused by them

			if      (top_frac > (INT64)CLAMPMAX) sprtopscreen = windowtop = CLAMPMAX;
			else if (top_frac > (INT64)CLAMPMIN) sprtopscreen = windowtop = (fixed_t)top_frac;
			else                                 sprtopscreen = windowtop = CLAMPMIN;
			if      (bottom_frac > (INT64)CLAMPMAX) sprbotscreen = windowbottom = CLAMPMAX;
			else if (bottom_frac > (INT64)CLAMPMIN) sprbotscreen = windowbottom = (fixed_t)bottom_frac;
			else                                    sprbotscreen = windowbottom = CLAMPMIN;

			top_frac += top_step;
			bottom_frac += bottom_step;
			maskedtopscreen = FIXED_TO_FLOAT(sprtopscreen);
			maskedbotscreen = FIXED_TO_FLOAT(sprbotscreen);

			// SoM: If column is out of range, why bother with it??
			if (windowbottom < topbounds || windowtop > bottombounds)
			{
				if (dc_numlights)
				{
					for (i = 0; i < dc_numlights; i++)
					{
						rlight = &dc_lightlist[i];
						rlight->height += rlight->heightstep;
						if (rlight->flags & FF_CUTLEVEL)
							rlight->botheight += rlight->botheightstep;
					}
				}
				maskedscale += rw_scalestep;
				continue;
			}

			dc_iscale = FLOAT_TO_FIXED(1.0f / maskedscale);

			// Get data for the column
			col = (column_t *)((UINT8 *)R_GetColumn(texnum,maskedtexturecol[dc_x]) - 3);

			// SoM: New code does not rely on R_DrawColumnShadowed_8 which
			// will (hopefully) put less strain on the stack.
			if (dc_numlights)
			{
				lighttable_t **xwalllights;
				fixed_t height;
				fixed_t bheight = 0;
				INT32 solid = 0;
				INT32 lighteffect = 0;

				for (i = 0; i < dc_numlights; i++)
				{
					// Check if the current light effects the colormap/lightlevel
					rlight = &dc_lightlist[i];
					lighteffect = !(dc_lightlist[i].flags & FF_NOSHADE);
					if (lighteffect)
					{
						lightnum = rlight->lightnum;

						if (lightnum < 0)
							xwalllights = scalelight[0];
						else if (lightnum >= LIGHTLEVELS)
							xwalllights = scalelight[LIGHTLEVELS-1];
						else
							xwalllights = scalelight[lightnum];

						pindex = FLOAT_TO_FIXED(maskedscale * LIGHTRESOLUTIONFLOAT)>>LIGHTSCALESHIFT;

						if (pindex >= MAXLIGHTSCALE)
							pindex = MAXLIGHTSCALE-1;

						if (pfloor->flags & FF_FOG)
						{
							if (pfloor->master->frontsector->extra_colormap)
								rlight->rcolormap = pfloor->master->frontsector->extra_colormap->colormap + (xwalllights[pindex] - colormaps);
							else
								rlight->rcolormap = xwalllights[pindex];
						}
						else
						{
							if (rlight->extra_colormap)
								rlight->rcolormap = rlight->extra_colormap->colormap + (xwalllights[pindex] - colormaps);
							else
								rlight->rcolormap = xwalllights[pindex];
						}
					}

					solid = 0; // don't carry over solid-cutting flag from the previous light

					// Check if the current light can cut the current 3D floor.
					if (rlight->flags & FF_CUTSOLIDS && !(pfloor->flags & FF_EXTRA))
						solid = 1;
					else if (rlight->flags & FF_CUTEXTRA && pfloor->flags & FF_EXTRA)
					{
						if (rlight->flags & FF_EXTRA)
						{
							// The light is from an extra 3D floor... Check the flags so
							// there are no undesired cuts.
							if ((rlight->flags & (FF_FOG|FF_SWIMMABLE)) == (pfloor->flags & (FF_FOG|FF_SWIMMABLE)))
								solid = 1;
						}
						else
							solid = 1;
					}
					else
						solid = 0;

					height = rlight->height;
					rlight->height += rlight->heightstep;

					if (solid)
					{
						bheight = rlight->botheight - (FRACUNIT >> 1);
						rlight->botheight += rlight->botheightstep;
					}

					if (height <= windowtop)
					{
						if (lighteffect)
							dc_colormap = rlight->rcolormap;
						if (solid && windowtop < bheight)
							windowtop = bheight;
						continue;
					}

					windowbottom = height;
					if (windowbottom >= sprbotscreen)
					{
						windowbottom = sprbotscreen;
						// draw the texture
						spryscale = FLOAT_TO_FIXED(maskedscale);
						colfunc_2s (col);
						for (i++; i < dc_numlights; i++)
						{
							rlight = &dc_lightlist[i];
							rlight->height += rlight->heightstep;
							if (rlight->flags & FF_CUTLEVEL)
								rlight->botheight += rlight->botheightstep;
						}
						continue;
					}
					// draw the texture
					spryscale = FLOAT_TO_FIXED(maskedscale);
					colfunc_2s (col);
					if (solid)
						windowtop = bheight;
					else
						windowtop = windowbottom + 1;
					if (lighteffect)
						dc_colormap = rlight->rcolormap;
				}
				windowbottom = sprbotscreen;
				// draw the texture, if there is any space left
				spryscale = FLOAT_TO_FIXED(maskedscale);
				if (windowtop < windowbottom)
					colfunc_2s (col);

				maskedscale += rw_scalestep;
				continue;
			}

			// calculate lighting
			pindex = FLOAT_TO_FIXED(maskedscale * LIGHTRESOLUTIONFLOAT)>>LIGHTSCALESHIFT;

			if (pindex >= MAXLIGHTSCALE)
				pindex = MAXLIGHTSCALE - 1;

			dc_colormap = walllights[pindex];

			if (pfloor->flags & FF_FOG && pfloor->master->frontsector->extra_colormap)
				dc_colormap = pfloor->master->frontsector->extra_colormap->colormap + (dc_colormap - colormaps);
			else if (frontsector->extra_colormap)
				dc_colormap = frontsector->extra_colormap->colormap + (dc_colormap - colormaps);

			// draw the texture
			spryscale = FLOAT_TO_FIXED(maskedscale);
			colfunc_2s (col);
			maskedscale += rw_scalestep;
		}
	}
	colfunc = colfuncs[BASEDRAWFUNC];

#undef CLAMPMAX
#undef CLAMPMIN
}

// R_ExpandPlaneY
//
// A simple function to modify a vsplane's top and bottom for a particular column
// Sort of like R_ExpandPlane in r_plane.c, except this is vertical expansion
static inline void R_ExpandPlaneY(visplane_t *pl, INT32 x, INT16 top, INT16 bottom)
{
	// Expand the plane, don't shrink it!
	// note: top and bottom default to 0xFFFF and 0x0000 respectively, which is totally compatible with this
	if (pl->top[x] > top)       pl->top[x] = top;
	if (pl->bottom[x] < bottom) pl->bottom[x] = bottom;
}

//
// R_RenderSegLoop
// Draws zero, one, or two textures (and possibly a masked
//  texture) for walls.
// Can draw or mark the starting pixel of floor and ceiling
//  textures.
// CALLED: CORE LOOPING ROUTINE.
//
//#define HEIGHTBITS              12
//#define HEIGHTUNIT              (1<<HEIGHTBITS)


//profile stuff ---------------------------------------------------------
//#define TIMING
#ifdef TIMING
#include "p5prof.h"
INT64 mycount;
INT64 mytotal = 0;
UINT32 nombre = 100000;
//static   char runtest[10][80];
#endif
//profile stuff ---------------------------------------------------------

static void R_RenderSegLoop (void)
{
	angle_t angle;
	size_t  pindex;
	INT32     yl;
	INT32     yh;

	INT32     mid;
	fixed_t texturecolumn = 0;
	float ftexturecolumn = 0.0f;
	float oldtexturecolumn = -1.0f;
	INT32     top;
	INT32     bottom;
	INT32     i;

	for (; rw_x < rw_stopx; rw_x++)
	{
		// mark floor / ceiling areas
		yl = (INT32)llrintf(floor(topfrac));

		top = ceilingclip[rw_x]+1;

		// no space above wall?
		if (yl < top)
			yl = top;

		if (markceiling)
		{
#if 0
			bottom = yl-1;

			if (bottom >= floorclip[rw_x])
				bottom = floorclip[rw_x]-1;

			if (top <= bottom)
#else
			bottom = yl > floorclip[rw_x] ? floorclip[rw_x] : yl;

			if (top <= --bottom && ceilingplane)
#endif
				R_ExpandPlaneY(ceilingplane, rw_x, top, bottom);
		}

		yh = (INT32)llrintf(floor(bottomfrac));
		bottom = floorclip[rw_x]-1;

		if (yh > bottom)
			yh = bottom;

		if (markfloor)
		{
			top = yh < ceilingclip[rw_x] ? ceilingclip[rw_x] : yh;

			if (++top <= bottom && floorplane)
				R_ExpandPlaneY(floorplane, rw_x, top, bottom);
		}

		if (numffloors)
		{
			firstseg->frontscale[rw_x] = frontscale[rw_x];
			top = ceilingclip[rw_x]+1; // PRBoom
			bottom = floorclip[rw_x]-1; // PRBoom

			for (i = 0; i < numffloors; i++)
			{
				if (ffloor[i].polyobj && (!curline->polyseg || ffloor[i].polyobj != curline->polyseg))
					continue;

				if (ffloor[i].height < viewz)
				{
					INT32 top_w = (INT32)llrintf(ffloor[i].f_frac);
					INT32 bottom_w = (INT32)llrintf(ffloor[i].f_clip[rw_x]);

					if (top_w < top)
						top_w = top;

					if (bottom_w > bottom)
						bottom_w = bottom;

					// Polyobject-specific hack to fix plane leaking -Red
					if (ffloor[i].polyobj && top_w >= bottom_w)
					{
						ffloor[i].plane->top[rw_x] = 0xFFFF;
						ffloor[i].plane->bottom[rw_x] = 0x0000; // fix for sky plane drawing crashes - Monster Iestyn 25/05/18
					}
					else
					{
						if (top_w <= bottom_w)
						{
							ffloor[i].plane->top[rw_x] = (INT16)top_w;
							ffloor[i].plane->bottom[rw_x] = (INT16)bottom_w;
						}
					}
				}
				else if (ffloor[i].height > viewz)
				{
					INT32 top_w = (INT32)llrintf(ffloor[i].c_clip[rw_x]);
					INT32 bottom_w = (INT32)llrintf(ffloor[i].f_frac);

					if (top_w < top)
						top_w = top;

					if (bottom_w > bottom)
						bottom_w = bottom;

					// Polyobject-specific hack to fix plane leaking -Red
					if (ffloor[i].polyobj && top_w >= bottom_w)
					{
						ffloor[i].plane->top[rw_x] = 0xFFFF;
						ffloor[i].plane->bottom[rw_x] = 0x0000; // fix for sky plane drawing crashes - Monster Iestyn 25/05/18
					}
					else
					{
						if (top_w <= bottom_w)
						{
							ffloor[i].plane->top[rw_x] = (INT16)top_w;
							ffloor[i].plane->bottom[rw_x] = (INT16)bottom_w;
						}
					}
				}
			}
		}

		//SoM: Calculate offsets for Thick fake floors.
		// calculate texture offset
		angle = (rw_centerangle + xtoviewangle[rw_x])>>ANGLETOFINESHIFT;
		ftexturecolumn = rw_offset - (FIXED_TO_FLOAT(FINETANGENT(angle))*rw_distance);

		if (FLOAT_INEQUALITY(oldtexturecolumn, -1.0f)) {
			float texcol = oldtexturecolumn-ftexturecolumn;
			rw_bottomtexturemid += (rw_bottomtextureslide*texcol);
			rw_midtexturemid    += (rw_midtextureslide*texcol);
			rw_toptexturemid    += (rw_toptextureslide*texcol);
			rw_midtextureback   += (rw_midtexturebackslide*texcol);
		}
		oldtexturecolumn = ftexturecolumn;
		texturecolumn = llrintf(ftexturecolumn);

		// texturecolumn and lighting are independent of wall tiers
		if (segtextured)
		{
			// calculate lighting
			pindex = FLOAT_TO_FIXED(rw_scale * LIGHTRESOLUTIONFLOAT)>>LIGHTSCALESHIFT;

			if (pindex >=  MAXLIGHTSCALE)
				pindex = MAXLIGHTSCALE-1;

			dc_colormap = walllights[pindex];
			dc_x = rw_x;
			dc_iscale = FLOAT_TO_FIXED(1.0f / rw_scale);

			if (frontsector->extra_colormap)
				dc_colormap = frontsector->extra_colormap->colormap + (dc_colormap - colormaps);
		}

		if (dc_numlights)
		{
			lighttable_t **xwalllights;
			for (i = 0; i < dc_numlights; i++)
			{
				INT32 lightnum;
				lightnum = (dc_lightlist[i].lightlevel >> LIGHTSEGSHIFT);

				if (dc_lightlist[i].extra_colormap)
					;
				else if (curline->v1->y == curline->v2->y)
					lightnum--;
				else if (curline->v1->x == curline->v2->x)
					lightnum++;

				if (lightnum < 0)
					xwalllights = scalelight[0];
				else if (lightnum >= LIGHTLEVELS)
					xwalllights = scalelight[LIGHTLEVELS-1];
				else
					xwalllights = scalelight[lightnum];

				pindex = FLOAT_TO_FIXED(rw_scale * LIGHTRESOLUTIONFLOAT)>>LIGHTSCALESHIFT;

				if (pindex >=  MAXLIGHTSCALE)
					pindex = MAXLIGHTSCALE-1;

				if (dc_lightlist[i].extra_colormap)
					dc_lightlist[i].rcolormap = dc_lightlist[i].extra_colormap->colormap + (xwalllights[pindex] - colormaps);
				else
					dc_lightlist[i].rcolormap = xwalllights[pindex];

				colfunc = colfuncs[COLDRAWFUNC_SHADOWED];
			}
		}

		frontscale[rw_x] = FLOAT_TO_FIXED(rw_scale);

		// draw the wall tiers
		if (midtexture)
		{
			// single sided line
			if (yl <= yh && yh >= 0 && yl < viewheight)
			{
				dc_yl = yl;
				dc_yh = yh;
				dc_texturemid = FLOAT_TO_FIXED(rw_midtexturemid);
				dc_source = R_GetColumn(midtexture,texturecolumn);
				dc_texheight = textureheight[midtexture]>>FRACBITS;

				//profile stuff ---------------------------------------------------------
#ifdef TIMING
				ProfZeroTimer();
#endif
				colfunc();
#ifdef TIMING
				RDMSR(0x10,&mycount);
				mytotal += mycount;      //64bit add

				if (nombre--==0)
					I_Error("R_DrawColumn CPU Spy reports: 0x%d %d\n", *((INT32 *)&mytotal+1),
						(INT32)mytotal);
#endif
				//profile stuff ---------------------------------------------------------

				// dont draw anything more for this column, since
				// a midtexture blocks the view
				ceilingclip[rw_x] = (INT16)viewheight;
				floorclip[rw_x] = -1;
			}
			else
			{
				// note: don't use min/max macros, since casting from INT32 to INT16 is involved here
				if (markceiling)
					ceilingclip[rw_x] = (yl >= 0) ? ((yl > viewheight) ? (INT16)viewheight : (INT16)((INT16)yl - 1)) : -1;
				if (markfloor)
					floorclip[rw_x] = (yh < viewheight) ? ((yh < -1) ? -1 : (INT16)((INT16)yh + 1)) : (INT16)viewheight;
			}
		}
		else
		{
			// two sided line
			if (toptexture)
			{
				// top wall
				mid = (INT32)llrintf(ceil(pixhigh));
				pixhigh += pixhighstep;

				if (mid >= floorclip[rw_x])
					mid = floorclip[rw_x]-1;

				if (mid >= yl) // back ceiling lower than front ceiling ?
				{
					if (yl >= viewheight) // entirely off bottom of screen
						ceilingclip[rw_x] = (INT16)viewheight;
					else if (mid >= 0) // safe to draw top texture
					{
						dc_yl = yl;
						dc_yh = mid;
						dc_texturemid = FLOAT_TO_FIXED(rw_toptexturemid);
						dc_source = R_GetColumn(toptexture,texturecolumn);
						dc_texheight = textureheight[toptexture]>>FRACBITS;
						colfunc();
						ceilingclip[rw_x] = (INT16)mid;
					}
					else // entirely off top of screen
						ceilingclip[rw_x] = -1;
				}
				else
					ceilingclip[rw_x] = (yl >= 0) ? ((yl > viewheight) ? (INT16)viewheight : (INT16)((INT16)yl - 1)) : -1;
			}
			else if (markceiling) // no top wall
				ceilingclip[rw_x] = (yl >= 0) ? ((yl > viewheight) ? (INT16)viewheight : (INT16)((INT16)yl - 1)) : -1;

			if (bottomtexture)
			{
				// bottom wall
				mid = (INT32)llrintf(ceil(pixlow));
				pixlow += pixlowstep;

				// no space above wall?
				if (mid <= ceilingclip[rw_x])
					mid = ceilingclip[rw_x]+1;

				if (mid <= yh) // back floor higher than front floor ?
				{
					if (yh < 0) // entirely off top of screen
						floorclip[rw_x] = -1;
					else if (mid < viewheight) // safe to draw bottom texture
					{
						dc_yl = mid;
						dc_yh = yh;
						dc_texturemid = FLOAT_TO_FIXED(rw_bottomtexturemid);
						dc_source = R_GetColumn(bottomtexture,
							texturecolumn);
						dc_texheight = textureheight[bottomtexture]>>FRACBITS;
						colfunc();
						floorclip[rw_x] = (INT16)mid;
					}
					else  // entirely off bottom of screen
						floorclip[rw_x] = (INT16)viewheight;
				}
				else
					floorclip[rw_x] = (yh < viewheight) ? ((yh < -1) ? -1 : (INT16)((INT16)yh + 1)) : (INT16)viewheight;
			}
			else if (markfloor) // no bottom wall
				floorclip[rw_x] = (yh < viewheight) ? ((yh < -1) ? -1 : (INT16)((INT16)yh + 1)) : (INT16)viewheight;
		}

		if (maskedtexture || numthicksides)
		{
			// save texturecol
			//  for backdrawing of masked mid texture
			maskedtexturecol[rw_x] = (INT16)texturecolumn;

			if (maskedtextureheight != NULL) {
				maskedtextureheight[rw_x] = (!!(curline->linedef->flags & ML_DONTPEGBOTTOM) ^ !!(curline->linedef->flags & ML_EFFECT3) ?
											max(rw_midtexturemid, rw_midtextureback) :
											min(rw_midtexturemid, rw_midtextureback));
			}
		}

		if (dc_numlights)
		{
			for (i = 0; i < dc_numlights; i++)
			{
				dc_lightlist[i].height += dc_lightlist[i].heightstep;
				if (dc_lightlist[i].flags & FF_CUTSOLIDS)
					dc_lightlist[i].botheight += dc_lightlist[i].botheightstep;
			}
		}

		for (i = 0; i < numffloors; i++)
			ffloor[i].f_frac += ffloor[i].f_step;

		for (i = 0; i < numbackffloors; i++)
		{
			ffloor[i].f_clip[rw_x] = ffloor[i].c_clip[rw_x] = ffloor[i].b_frac;
			ffloor[i].b_frac += ffloor[i].b_step;
		}

		rw_scale += rw_scalestep;
		topfrac += topstep;
		bottomfrac += bottomstep;
	}
}

static float R_CalcSegDistFloat(seg_t *seg, float x2, float y2, boolean overflow)
{
	float v1x = FIXED_TO_FLOAT(seg->v1->x);
	float v1y = FIXED_TO_FLOAT(seg->v1->y);
	float v2x = FIXED_TO_FLOAT(seg->v2->x);
	float v2y = FIXED_TO_FLOAT(seg->v2->y);
	float dx, dy, vdx, vdy;

	// The seg is vertical.
	if (!seg->linedef->dy)
		rw_distance = fabsf(y2 - v1y);
	// The seg is horizontal.
	else if (!seg->linedef->dx)
		rw_distance = fabsf(x2 - v1x);
	// Uses precalculated seg->length
	else if (overflow)
	{
		dx = v2x-v1x;
		dy = v2y-v1y;
		vdx = x2-v1x;
		vdy = y2-v1y;
		rw_distance = ((dy*vdx)-(dx*vdy))/(seg->flength);
	}
	// Linguica's fix converted to floating-point math
	else
	{
		fixed_t x, y, distance;
		float a, c, ac;

		v1x -= FIXED_TO_FLOAT(viewx);
		v1y -= FIXED_TO_FLOAT(viewy);
		v2x -= FIXED_TO_FLOAT(viewx);
		v2y -= FIXED_TO_FLOAT(viewy);
		dx = v2x - v1x;
		dy = v2y - v1y;

		a = (v1x*v2y) - (v1y*v2x);
		c = (dx*dx) + (dy*dy);
		ac = (a/c);

		x = FLOAT_TO_FIXED(ac*(-dy));
		y = FLOAT_TO_FIXED(ac*dx);

		distance = R_PointToDist(viewx + x, viewy + y);
		rw_distance = FIXED_TO_FLOAT(distance);
	}

	return rw_distance;
}

//
// R_StoreWallRange
// A wall segment will be drawn
//  between start and stop pixels (inclusive).
//
void R_StoreWallRange(INT32 start, INT32 stop)
{
	fixed_t       hyp;
	fixed_t       sineval;
	angle_t       distangle, offsetangle;
	boolean overflow;
	INT32           lightnum;
	INT32           i, p;
	lightlist_t   *light;
	r_lightlist_t *rlight;
	float range;
	vertex_t segleft, segright;
	fixed_t ceilingfrontslide, floorfrontslide, ceilingbackslide, floorbackslide;
	static size_t maxdrawsegs = 0;

	maskedtextureheight = NULL;
	//initialize segleft and segright
	memset(&segleft, 0x00, sizeof(segleft));
	memset(&segright, 0x00, sizeof(segright));

	colfunc = colfuncs[BASEDRAWFUNC];

	if (ds_p == drawsegs+maxdrawsegs)
	{
		size_t curpos = curdrawsegs - drawsegs;
		size_t pos = ds_p - drawsegs;
		size_t newmax = maxdrawsegs ? maxdrawsegs*2 : 128;
		if (firstseg)
			firstseg = (drawseg_t *)(firstseg - drawsegs);
		drawsegs = Z_Realloc(drawsegs, newmax*sizeof (*drawsegs), PU_STATIC, NULL);
		ds_p = drawsegs + pos;
		maxdrawsegs = newmax;
		curdrawsegs = drawsegs + curpos;
		if (firstseg)
			firstseg = drawsegs + (size_t)firstseg;
	}

	sidedef = curline->sidedef;
	linedef = curline->linedef;

	// calculate rw_distance for scale calculation
	rw_normalangle = curline->angle + ANGLE_90;
	offsetangle = abs((INT32)(rw_normalangle-rw_angle1));

	if (offsetangle > ANGLE_90)
		offsetangle = ANGLE_90;

	distangle = ANGLE_90 - offsetangle;
	sineval = FINESINE(distangle>>ANGLETOFINESHIFT);
	hyp = R_PointToDist(curline->v1->x, curline->v1->y);
	overflow = (hyp >= INT32_MAX);

	// The seg is vertical.
	if (curline->v1->y == curline->v2->y)
	{
		// Does the compiler complain if I use abs() inside a macro?
		fixed_t distance = abs(viewy - curline->v1->y);
		rw_distance = FIXED_TO_FLOAT(distance);
	}
	// The seg is horizontal.
	else if (curline->v1->x == curline->v2->x)
	{
		// Like this:
		// rw_distance = FIXED_TO_FLOAT(abs(viewx - curline->v1->x));
		fixed_t distance = abs(viewx - curline->v1->x);
		rw_distance = FIXED_TO_FLOAT(distance);
	}
	// big room fix
	else if ((curline->length >= 1024<<FRACBITS) || overflow)
		rw_distance = R_CalcSegDistFloat(curline, FIXED_TO_FLOAT(viewx), FIXED_TO_FLOAT(viewy), overflow);
	else
		rw_distance = FIXED_TO_FLOAT(hyp) * FIXED_TO_FLOAT(sineval);

	ds_p->x1 = rw_x = start;
	ds_p->x2 = stop;
	ds_p->curline = curline;
	rw_stopx = stop+1;

	//SoM: Code to remove limits on openings.
	{
		size_t pos = lastopening - openings;
		size_t need = (rw_stopx - start)*4 + pos;
		if (need > maxopenings)
		{
			drawseg_t *ds;  //needed for fix from *cough* zdoom *cough*
			INT16 *oldopenings = openings;
			INT16 *oldlast = lastopening;

			do
				maxopenings = maxopenings ? maxopenings*2 : 16384;
			while (need > maxopenings);
			openings = Z_Realloc(openings, maxopenings * sizeof (*openings), PU_STATIC, NULL);
			lastopening = openings + pos;

			// borrowed fix from *cough* zdoom *cough*
			// [RH] We also need to adjust the openings pointers that
			//    were already stored in drawsegs.
			for (ds = drawsegs; ds < ds_p; ds++)
			{
#define ADJUST(p) if (ds->p + ds->x1 >= oldopenings && ds->p + ds->x1 <= oldlast) ds->p = ds->p - oldopenings + openings;
				ADJUST(maskedtexturecol);
				ADJUST(sprtopclip);
				ADJUST(sprbottomclip);
				ADJUST(thicksidecol);
#undef ADJUST
			}
		}
	}  // end of code to remove limits on openings

	// calculate scale at both ends and step
	ds_p->scale1 = rw_scale = R_ScaleFromGlobalAngleFloat(viewangle + xtoviewangle[start]);

	if (stop > start)
	{
		ds_p->scale2 = R_ScaleFromGlobalAngleFloat(viewangle + xtoviewangle[stop]);
		range = (float)(stop-start);
	}
	else
	{
		// UNUSED: try to fix the stretched line bug
#if 0
		if (rw_distance < 0.5f)
		{
			fixed_t         tr_x,tr_y;
			fixed_t         gxt,gyt;
			CONS_Debug(DBG_RENDER, "TRYING TO FIX THE STRETCHED ETC\n");

			tr_x = curline->v1->x - viewx;
			tr_y = curline->v1->y - viewy;

			gxt = FixedMul(tr_x, viewcos);
			gyt = -FixedMul(tr_y, viewsin);
			ds_p->scale1 = FixedDiv(projection, gxt - gyt);
		}
#endif
		ds_p->scale2 = ds_p->scale1;
		range = 1.0f;
	}

	ds_p->scalestep = rw_scalestep = (ds_p->scale2 - ds_p->scale1) / range;

	// calculate texture boundaries
	//  and decide if floor / ceiling marks are needed
	// Figure out map coordinates of where start and end are mapping to on seg, so we can clip right for slope bullshit
	if (frontsector->hasslope || (backsector && backsector->hasslope)) // Commenting this out for FOFslop. -Red
	{
		angle_t temp;

		// left
		temp = xtoviewangle[start]+viewangle;

		{
			// Both lines can be written in slope-intercept form, so figure out line intersection
			float a1, b1, c1, a2, b2, c2, det; // 1 is the seg, 2 is the view angle vector...
			///TODO: convert to FPU

			a1 = FIXED_TO_FLOAT(curline->v2->y-curline->v1->y);
			b1 = FIXED_TO_FLOAT(curline->v1->x-curline->v2->x);
			c1 = a1*FIXED_TO_FLOAT(curline->v1->x) + b1*FIXED_TO_FLOAT(curline->v1->y);

			a2 = -FIXED_TO_FLOAT(FINESINE(temp>>ANGLETOFINESHIFT));
			b2 = FIXED_TO_FLOAT(FINECOSINE(temp>>ANGLETOFINESHIFT));
			c2 = a2*FIXED_TO_FLOAT(viewx) + b2*FIXED_TO_FLOAT(viewy);

			det = a1*b2 - a2*b1;

			ds_p->leftpos.x = segleft.x = FLOAT_TO_FIXED((b2*c1 - b1*c2)/det);
			ds_p->leftpos.y = segleft.y = FLOAT_TO_FIXED((a1*c2 - a2*c1)/det);
		}

		// right
		temp = xtoviewangle[stop]+viewangle;

		{
			// Both lines can be written in slope-intercept form, so figure out line intersection
			float a1, b1, c1, a2, b2, c2, det; // 1 is the seg, 2 is the view angle vector...
			///TODO: convert to FPU

			a1 = FIXED_TO_FLOAT(curline->v2->y-curline->v1->y);
			b1 = FIXED_TO_FLOAT(curline->v1->x-curline->v2->x);
			c1 = a1*FIXED_TO_FLOAT(curline->v1->x) + b1*FIXED_TO_FLOAT(curline->v1->y);

			a2 = -FIXED_TO_FLOAT(FINESINE(temp>>ANGLETOFINESHIFT));
			b2 = FIXED_TO_FLOAT(FINECOSINE(temp>>ANGLETOFINESHIFT));
			c2 = a2*FIXED_TO_FLOAT(viewx) + b2*FIXED_TO_FLOAT(viewy);

			det = a1*b2 - a2*b1;

			ds_p->rightpos.x = segright.x = FLOAT_TO_FIXED((b2*c1 - b1*c2)/det);
			ds_p->rightpos.y = segright.y = FLOAT_TO_FIXED((a1*c2 - a2*c1)/det);
		}
	}

#define SLOPEPARAMS(slope, end1, end2, normalheight) \
	if (slope) { \
		end1 = P_GetZAt(slope, segleft.x, segleft.y); \
		end2 = P_GetZAt(slope, segright.x, segright.y); \
	} else \
		end1 = end2 = normalheight;

	SLOPEPARAMS(frontsector->c_slope, worldtopfixed,    worldtopslopefixed,    frontsector->ceilingheight)
	SLOPEPARAMS(frontsector->f_slope, worldbottomfixed, worldbottomslopefixed, frontsector->floorheight)

	// subtract viewz from these to turn them into
	// positions relative to the camera's z position
	worldtopfixed -= viewz;
	worldtopslopefixed -= viewz;
	worldbottomfixed -= viewz;
	worldbottomslopefixed -= viewz;

	worldtop = FIXED_TO_FLOAT(worldtopfixed);
	worldtopslope = FIXED_TO_FLOAT(worldtopslopefixed);
	worldbottom = FIXED_TO_FLOAT(worldbottomfixed);
	worldbottomslope = FIXED_TO_FLOAT(worldbottomslopefixed);

	midtexture = toptexture = bottomtexture = maskedtexture = 0;
	ds_p->maskedtexturecol = NULL;
	ds_p->numthicksides = numthicksides = 0;
	ds_p->thicksidecol = NULL;
	ds_p->tsilheight = 0;

	numbackffloors = 0;

	for (i = 0; i < MAXFFLOORS; i++)
		ds_p->thicksides[i] = NULL;

	if (numffloors)
	{
		for (i = 0; i < numffloors; i++)
		{
			if (ffloor[i].polyobj && (!ds_p->curline->polyseg || ffloor[i].polyobj != ds_p->curline->polyseg))
				continue;

			if (ffloor[i].slope) {
				ffloor[i].f_pos = FIXED_TO_FLOAT(P_GetZAt(ffloor[i].slope, segleft.x, segleft.y) - viewz);
				ffloor[i].f_pos_slope = FIXED_TO_FLOAT(P_GetZAt(ffloor[i].slope, segright.x, segright.y) - viewz);
			}
			else
				ffloor[i].f_pos = ffloor[i].f_pos_slope = FIXED_TO_FLOAT(ffloor[i].height - viewz);
		}
	}

	// Set up texture Y offset slides for sloped walls
	rw_toptextureslide = rw_midtextureslide = rw_bottomtextureslide = 0.0f;
	ceilingfrontslide = floorfrontslide = ceilingbackslide = floorbackslide = 0;

	{
		angle_t lineangle = R_PointToAngle2(curline->v1->x, curline->v1->y, curline->v2->x, curline->v2->y);

		if (frontsector->f_slope)
			floorfrontslide = FixedMul(frontsector->f_slope->zdelta, FINECOSINE((lineangle-frontsector->f_slope->xydirection)>>ANGLETOFINESHIFT));

		if (frontsector->c_slope)
			ceilingfrontslide = FixedMul(frontsector->c_slope->zdelta, FINECOSINE((lineangle-frontsector->c_slope->xydirection)>>ANGLETOFINESHIFT));

		if (backsector && backsector->f_slope)
			floorbackslide = FixedMul(backsector->f_slope->zdelta, FINECOSINE((lineangle-backsector->f_slope->xydirection)>>ANGLETOFINESHIFT));

		if (backsector && backsector->c_slope)
			ceilingbackslide = FixedMul(backsector->c_slope->zdelta, FINECOSINE((lineangle-backsector->c_slope->xydirection)>>ANGLETOFINESHIFT));
	}

	if (!backsector)
	{
		float texheight;

		// single sided line
		midtexture = R_GetTextureNum(sidedef->midtexture);
		texheight = FIXED_TO_FLOAT(textureheight[midtexture]);

		// a single sided line is terminal, so it must mark ends
		markfloor = markceiling = true;
		if (linedef->flags & ML_EFFECT2) {
			if (linedef->flags & ML_DONTPEGBOTTOM)
				rw_midtexturemid = FIXED_TO_FLOAT(frontsector->floorheight + texheight - viewz);
			else
				rw_midtexturemid = FIXED_TO_FLOAT(frontsector->ceilingheight - viewz);
		}
		else if (linedef->flags & ML_DONTPEGBOTTOM)
		{
			rw_midtexturemid = worldbottom + texheight;
			rw_midtextureslide = FIXED_TO_FLOAT(floorfrontslide);
		}
		else
		{
			// top of texture at top
			rw_midtexturemid = worldtop;
			rw_midtextureslide = FIXED_TO_FLOAT(ceilingfrontslide);
		}

		rw_midtexturemid += FIXED_TO_FLOAT(sidedef->rowoffset);

		ds_p->silhouette = SIL_BOTH;
		ds_p->sprtopclip = screenheightarray;
		ds_p->sprbottomclip = negonearray;
		ds_p->bsilheight = INT32_MAX;
		ds_p->tsilheight = INT32_MIN;
	}
	else
	{
		// two sided line
		boolean bothceilingssky = false; // turned on if both back and front ceilings are sky
		boolean bothfloorssky = false; // likewise, but for floors

		SLOPEPARAMS(backsector->c_slope, worldhighfixed, worldhighslopefixed, backsector->ceilingheight)
		SLOPEPARAMS(backsector->f_slope, worldlowfixed,  worldlowslopefixed,  backsector->floorheight)

		worldhighfixed -= viewz;
		worldhighslopefixed -= viewz;
		worldlowfixed -= viewz;
		worldlowslopefixed -= viewz;

		worldhigh = FIXED_TO_FLOAT(worldhighfixed);
		worldhighslope = FIXED_TO_FLOAT(worldhighslopefixed);
		worldlow = FIXED_TO_FLOAT(worldlowfixed);
		worldlowslope = FIXED_TO_FLOAT(worldlowslopefixed);

		// hack to allow height changes in outdoor areas
		// This is what gets rid of the upper textures if there should be sky
		if (frontsector->ceilingpic == skyflatnum
			&& backsector->ceilingpic == skyflatnum)
		{
			bothceilingssky = true;
		}

		// likewise, but for floors and upper textures
		if (frontsector->floorpic == skyflatnum
			&& backsector->floorpic == skyflatnum)
		{
			bothfloorssky = true;
		}

		ds_p->sprtopclip = ds_p->sprbottomclip = NULL;
		ds_p->silhouette = 0;

		if (!bothfloorssky)
		{
			if (worldbottomslopefixed > worldlowslopefixed || worldbottomfixed > worldlowfixed)
			{
				ds_p->silhouette = SIL_BOTTOM;
				if ((backsector->f_slope ? P_GetZAt(backsector->f_slope, viewx, viewy) : backsector->floorheight) > viewz)
					ds_p->bsilheight = INT32_MAX;
				else
					ds_p->bsilheight = (frontsector->f_slope ? INT32_MAX : frontsector->floorheight);
			}
			else if ((backsector->f_slope ? P_GetZAt(backsector->f_slope, viewx, viewy) : backsector->floorheight) > viewz)
			{
				ds_p->silhouette = SIL_BOTTOM;
				ds_p->bsilheight = INT32_MAX;
				// ds_p->sprbottomclip = negonearray;
			}
		}

		if (!bothceilingssky)
		{
			if (worldtopslopefixed < worldhighslopefixed || worldtopfixed < worldhighfixed)
			{
				ds_p->silhouette |= SIL_TOP;
				if ((backsector->c_slope ? P_GetZAt(backsector->c_slope, viewx, viewy) : backsector->ceilingheight) < viewz)
					ds_p->tsilheight = INT32_MIN;
				else
					ds_p->tsilheight = (frontsector->c_slope ? INT32_MIN : frontsector->ceilingheight);
			}
			else if ((backsector->c_slope ? P_GetZAt(backsector->c_slope, viewx, viewy) : backsector->ceilingheight) < viewz)
			{
				ds_p->silhouette |= SIL_TOP;
				ds_p->tsilheight = INT32_MIN;
				// ds_p->sprtopclip = screenheightarray;
			}
		}

		if (!bothceilingssky && !bothfloorssky)
		{
			if (worldhighfixed <= worldbottomfixed && worldhighslopefixed <= worldbottomslopefixed)
			{
				ds_p->sprbottomclip = negonearray;
				ds_p->bsilheight = INT32_MAX;
				ds_p->silhouette |= SIL_BOTTOM;
			}

			if (worldlowfixed >= worldtopfixed && worldlowslopefixed >= worldtopslopefixed)
			{
				ds_p->sprtopclip = screenheightarray;
				ds_p->tsilheight = INT32_MIN;
				ds_p->silhouette |= SIL_TOP;
			}
		}

		//SoM: 3/25/2000: This code fixes an automap bug that didn't check
		// frontsector->ceiling and backsector->floor to see if a door was closed.
		// Without the following code, sprites get displayed behind closed doors.
		if (!bothceilingssky && !bothfloorssky)
		{
			if (doorclosed || (worldhighfixed <= worldbottomfixed && worldhighslopefixed <= worldbottomslopefixed))
			{
				ds_p->sprbottomclip = negonearray;
				ds_p->bsilheight = INT32_MAX;
				ds_p->silhouette |= SIL_BOTTOM;
			}
			if (doorclosed || (worldlowfixed >= worldtopfixed && worldlowslopefixed >= worldtopslopefixed))
			{                   // killough 1/17/98, 2/8/98
				ds_p->sprtopclip = screenheightarray;
				ds_p->tsilheight = INT32_MIN;
				ds_p->silhouette |= SIL_TOP;
			}
		}

		if (bothfloorssky)
		{
			// see double ceiling skies comment
			// this is the same but for upside down thok barriers where the floor is sky and the ceiling is normal
			markfloor = false;
		}
		else if (worldlowfixed != worldbottomfixed
			|| worldlowslopefixed != worldbottomslopefixed
			|| backsector->f_slope != frontsector->f_slope
		    || backsector->floorpic != frontsector->floorpic
		    || backsector->lightlevel != frontsector->lightlevel
		    //SoM: 3/22/2000: Check floor x and y offsets.
		    || backsector->floor_xoffs != frontsector->floor_xoffs
		    || backsector->floor_yoffs != frontsector->floor_yoffs
		    || backsector->floorpic_angle != frontsector->floorpic_angle
		    //SoM: 3/22/2000: Prevents bleeding.
		    || (frontsector->heightsec != -1 && frontsector->floorpic != skyflatnum)
		    || backsector->floorlightsec != frontsector->floorlightsec
		    //SoM: 4/3/2000: Check for colormaps
		    || frontsector->extra_colormap != backsector->extra_colormap
		    || (frontsector->ffloors != backsector->ffloors && frontsector->tag != backsector->tag))
		{
			markfloor = true;
		}
		else
		{
			// same plane on both sides
			markfloor = false;
		}

		if (bothceilingssky)
		{
			// double ceiling skies are special
			// we don't want to lower the ceiling clipping, (no new plane is drawn anyway)
			// so we can see the floor of thok barriers always regardless of sector properties
			markceiling = false;
		}
		else if (worldhighfixed != worldtopfixed
			|| worldhighslopefixed != worldtopslopefixed
			|| backsector->c_slope != frontsector->c_slope
		    || backsector->ceilingpic != frontsector->ceilingpic
		    || backsector->lightlevel != frontsector->lightlevel
		    //SoM: 3/22/2000: Check floor x and y offsets.
		    || backsector->ceiling_xoffs != frontsector->ceiling_xoffs
		    || backsector->ceiling_yoffs != frontsector->ceiling_yoffs
		    || backsector->ceilingpic_angle != frontsector->ceilingpic_angle
		    //SoM: 3/22/2000: Prevents bleeding.
		    || (frontsector->heightsec != -1 && frontsector->ceilingpic != skyflatnum)
		    || backsector->ceilinglightsec != frontsector->ceilinglightsec
		    //SoM: 4/3/2000: Check for colormaps
		    || frontsector->extra_colormap != backsector->extra_colormap
		    || (frontsector->ffloors != backsector->ffloors && frontsector->tag != backsector->tag))
		{
				markceiling = true;
		}
		else
		{
			// same plane on both sides
			markceiling = false;
		}

		if (!bothceilingssky && !bothfloorssky)
		{
			if ((worldhighfixed <= worldbottomfixed && worldhighslopefixed <= worldbottomslopefixed)
			 || (worldlowfixed >= worldtopfixed && worldlowslopefixed >= worldtopslopefixed))
			{
				// closed door
				markceiling = markfloor = true;
			}
		}

		// check TOP TEXTURE
		if (!bothceilingssky // never draw the top texture if on
			&& (worldhighfixed < worldtopfixed || worldhighslopefixed < worldtopslopefixed))
		{
			float texheight;

			// top texture
			if ((linedef->flags & (ML_DONTPEGTOP) && (linedef->flags & ML_DONTPEGBOTTOM))
				&& linedef->sidenum[1] != 0xffff)
			{
				// Special case... use offsets from 2nd side but only if it has a texture.
				side_t *def = &sides[linedef->sidenum[1]];
				toptexture = R_GetTextureNum(def->toptexture);

				if (!toptexture) //Second side has no texture, use the first side's instead.
					toptexture = R_GetTextureNum(sidedef->toptexture);
				texheight = FIXED_TO_FLOAT(textureheight[toptexture]);
			}
			else
			{
				toptexture = R_GetTextureNum(sidedef->toptexture);
				texheight = FIXED_TO_FLOAT(textureheight[toptexture]);
			}

			if (!(linedef->flags & ML_EFFECT1)) { // Ignore slopes for lower/upper textures unless flag is checked
				if (linedef->flags & ML_DONTPEGTOP)
					rw_toptexturemid = FIXED_TO_FLOAT(frontsector->ceilingheight - viewz);
				else
					rw_toptexturemid = FIXED_TO_FLOAT(backsector->ceilingheight - viewz);
			}
			else if (linedef->flags & ML_DONTPEGTOP)
			{
				// top of texture at top
				rw_toptexturemid = worldtop;
				rw_toptextureslide = FIXED_TO_FLOAT(ceilingfrontslide);
			}
			else
			{
				rw_toptexturemid = worldhigh + texheight;
				rw_toptextureslide = FIXED_TO_FLOAT(ceilingbackslide);
			}
		}

		// check BOTTOM TEXTURE
		if (!bothfloorssky // never draw the bottom texture if on
			&& (worldlowfixed > worldbottomfixed || worldlowslopefixed > worldbottomslopefixed)) // Only if VISIBLE!!!
		{
			// bottom texture
			bottomtexture = R_GetTextureNum(sidedef->bottomtexture);

			if (!(linedef->flags & ML_EFFECT1)) { // Ignore slopes for lower/upper textures unless flag is checked
				if (linedef->flags & ML_DONTPEGBOTTOM)
					rw_bottomtexturemid = FIXED_TO_FLOAT(frontsector->floorheight - viewz);
				else
					rw_bottomtexturemid = FIXED_TO_FLOAT(backsector->floorheight - viewz);
			}
			else if (linedef->flags & ML_DONTPEGBOTTOM)
			{
				// bottom of texture at bottom
				// top of texture at top
				rw_bottomtexturemid = worldbottom;
				rw_bottomtextureslide = FIXED_TO_FLOAT(floorfrontslide);
			}
			else
			{
				// top of texture at top
				rw_bottomtexturemid = worldlow;
				rw_bottomtextureslide = FIXED_TO_FLOAT(floorbackslide);
			}
		}

		rw_toptexturemid += FIXED_TO_FLOAT(sidedef->rowoffset);
		rw_bottomtexturemid += FIXED_TO_FLOAT(sidedef->rowoffset);

		// allocate space for masked texture tables
		if (frontsector && backsector && frontsector->tag != backsector->tag && (backsector->ffloors || frontsector->ffloors))
		{
			ffloor_t *rover;
			ffloor_t *r2;
			fixed_t   lowcut, highcut;
			fixed_t lowcutslope, highcutslope;

			// Used for height comparisons and etc across FOFs and slopes
			fixed_t high1, highslope1, low1, lowslope1, high2, highslope2, low2, lowslope2;

			//markceiling = markfloor = true;
			maskedtexture = true;

			ds_p->thicksidecol = maskedtexturecol = lastopening - rw_x;
			lastopening += rw_stopx - rw_x;

			lowcut = max(worldbottomfixed, worldlowfixed) + viewz;
			highcut = min(worldtopfixed, worldhighfixed) + viewz;
			lowcutslope = max(worldbottomslopefixed, worldlowslopefixed) + viewz;
			highcutslope = min(worldtopslopefixed, worldhighslopefixed) + viewz;

			if (frontsector->ffloors && backsector->ffloors)
			{
				i = 0;
				for (rover = backsector->ffloors; rover && i < MAXFFLOORS; rover = rover->next)
				{
					if (!(rover->flags & FF_RENDERSIDES) || !(rover->flags & FF_EXISTS))
						continue;
					if (!(rover->flags & FF_ALLSIDES) && rover->flags & FF_INVERTSIDES)
						continue;

					if (rover->norender == leveltime)
						continue;

					SLOPEPARAMS(*rover->t_slope, high1, highslope1, *rover->topheight)
					SLOPEPARAMS(*rover->b_slope, low1,  lowslope1,  *rover->bottomheight)

					if ((high1 < lowcut && highslope1 < lowcutslope) || (low1 > highcut && lowslope1 > highcutslope))
						continue;

					for (r2 = frontsector->ffloors; r2; r2 = r2->next)
					{
						if (!(r2->flags & FF_EXISTS) || !(r2->flags & FF_RENDERSIDES))
							continue;

						if (r2->norender == leveltime)
							continue;

						if (rover->flags & FF_EXTRA)
						{
							if (!(r2->flags & FF_CUTEXTRA))
								continue;

							if (r2->flags & FF_EXTRA && (r2->flags & (FF_TRANSLUCENT|FF_FOG)) != (rover->flags & (FF_TRANSLUCENT|FF_FOG)))
								continue;
						}
						else
						{
							if (!(r2->flags & FF_CUTSOLIDS))
								continue;
						}

						SLOPEPARAMS(*r2->t_slope, high2, highslope2, *r2->topheight)
						SLOPEPARAMS(*r2->b_slope, low2,  lowslope2,  *r2->bottomheight)

						if ((high2 < lowcut || highslope2 < lowcutslope) || (low2 > highcut || lowslope2 > highcutslope))
							continue;
						if ((high1 > high2 || highslope1 > highslope2) || (low1 < low2 || lowslope1 < lowslope2))
							continue;

						break;
					}
					if (r2)
						continue;

					ds_p->thicksides[i] = rover;
					i++;
				}

				for (rover = frontsector->ffloors; rover && i < MAXFFLOORS; rover = rover->next)
				{
					if (!(rover->flags & FF_RENDERSIDES) || !(rover->flags & FF_EXISTS))
						continue;
					if (!(rover->flags & FF_ALLSIDES || rover->flags & FF_INVERTSIDES))
						continue;

					if (rover->norender == leveltime)
						continue;

					SLOPEPARAMS(*rover->t_slope, high1, highslope1, *rover->topheight)
					SLOPEPARAMS(*rover->b_slope, low1,  lowslope1,  *rover->bottomheight)

					if ((high1 < lowcut && highslope1 < lowcutslope) || (low1 > highcut && lowslope1 > highcutslope))
						continue;

					for (r2 = backsector->ffloors; r2; r2 = r2->next)
					{
						if (!(r2->flags & FF_EXISTS) || !(r2->flags & FF_RENDERSIDES))
							continue;

						if (r2->norender == leveltime)
							continue;

						if (rover->flags & FF_EXTRA)
						{
							if (!(r2->flags & FF_CUTEXTRA))
								continue;

							if (r2->flags & FF_EXTRA && (r2->flags & (FF_TRANSLUCENT|FF_FOG)) != (rover->flags & (FF_TRANSLUCENT|FF_FOG)))
								continue;
						}
						else
						{
							if (!(r2->flags & FF_CUTSOLIDS))
								continue;
						}

						SLOPEPARAMS(*r2->t_slope, high2, highslope2, *r2->topheight)
						SLOPEPARAMS(*r2->b_slope, low2,  lowslope2,  *r2->bottomheight)
#undef SLOPEPARAMS
						if ((high2 < lowcut || highslope2 < lowcutslope) || (low2 > highcut || lowslope2 > highcutslope))
							continue;
						if ((high1 > high2 || highslope1 > highslope2) || (low1 < low2 || lowslope1 < lowslope2))
							continue;

						break;
					}
					if (r2)
						continue;

					ds_p->thicksides[i] = rover;
					i++;
				}
			}
			else if (backsector->ffloors)
			{
				for (rover = backsector->ffloors, i = 0; rover && i < MAXFFLOORS; rover = rover->next)
				{
					if (!(rover->flags & FF_RENDERSIDES) || !(rover->flags & FF_EXISTS))
						continue;
					if (!(rover->flags & FF_ALLSIDES) && rover->flags & FF_INVERTSIDES)
						continue;
					if (rover->norender == leveltime)
						continue;

					// Oy vey.
					if ((	   (*rover->t_slope ? P_GetZAt(*rover->t_slope, segleft.x, segleft.y) : *rover->topheight) <= worldbottomfixed+viewz
							&& (*rover->t_slope ? P_GetZAt(*rover->t_slope, segright.x, segright.y) : *rover->topheight) <= worldbottomslopefixed+viewz)
							||((*rover->b_slope ? P_GetZAt(*rover->b_slope, segleft.x, segleft.y) : *rover->bottomheight) >= worldtopfixed+viewz
							&& (*rover->b_slope ? P_GetZAt(*rover->b_slope, segright.x, segright.y) : *rover->bottomheight) >= worldtopslopefixed+viewz))
						continue;

					ds_p->thicksides[i] = rover;
					i++;
				}
			}
			else if (frontsector->ffloors)
			{
				for (rover = frontsector->ffloors, i = 0; rover && i < MAXFFLOORS; rover = rover->next)
				{
					if (!(rover->flags & FF_RENDERSIDES) || !(rover->flags & FF_EXISTS))
						continue;
					if (!(rover->flags & FF_ALLSIDES || rover->flags & FF_INVERTSIDES))
						continue;
					if (rover->norender == leveltime)
						continue;
					// Oy vey.
					if ((	   (*rover->t_slope ? P_GetZAt(*rover->t_slope, segleft.x, segleft.y) : *rover->topheight) <= worldbottomfixed+viewz
							&& (*rover->t_slope ? P_GetZAt(*rover->t_slope, segright.x, segright.y) : *rover->topheight) <= worldbottomslopefixed+viewz)
							||((*rover->b_slope ? P_GetZAt(*rover->b_slope, segleft.x, segleft.y) : *rover->bottomheight) >= worldtopfixed+viewz
							&& (*rover->b_slope ? P_GetZAt(*rover->b_slope, segright.x, segright.y) : *rover->bottomheight) >= worldtopslopefixed+viewz))
						continue;

					if ((	   (*rover->t_slope ? P_GetZAt(*rover->t_slope, segleft.x, segleft.y) : *rover->topheight) <= worldlowfixed+viewz
							&& (*rover->t_slope ? P_GetZAt(*rover->t_slope, segright.x, segright.y) : *rover->topheight) <= worldlowslopefixed+viewz)
							||((*rover->b_slope ? P_GetZAt(*rover->b_slope, segleft.x, segleft.y) : *rover->bottomheight) >= worldhighfixed+viewz
							&& (*rover->b_slope ? P_GetZAt(*rover->b_slope, segright.x, segright.y) : *rover->bottomheight) >= worldhighslopefixed+viewz))
						continue;

					ds_p->thicksides[i] = rover;
					i++;
				}
			}

			ds_p->numthicksides = numthicksides = i;
		}
		if (sidedef->midtexture > 0 && sidedef->midtexture < numtextures)
		{
			// masked midtexture
			if (!ds_p->thicksidecol)
			{
				ds_p->maskedtexturecol = maskedtexturecol = lastopening - rw_x;
				lastopening += rw_stopx - rw_x;
			}
			else
				ds_p->maskedtexturecol = ds_p->thicksidecol;

			maskedtextureheight = ds_p->maskedtextureheight; // note to red, this == &(ds_p->maskedtextureheight[0])

			if (curline->polyseg)
			{ // use REAL front and back floors please, so midtexture rendering isn't mucked up
				rw_midtextureslide = rw_midtexturebackslide = 0.0f;
				if (!!(linedef->flags & ML_DONTPEGBOTTOM) ^ !!(linedef->flags & ML_EFFECT3))
					rw_midtexturemid = rw_midtextureback = FIXED_TO_FLOAT(max(curline->frontsector->floorheight, curline->backsector->floorheight) - viewz);
			}
			// Set midtexture starting height
			else if (linedef->flags & ML_EFFECT2)
			{ // Ignore slopes when texturing
				rw_midtextureslide = rw_midtexturebackslide = 0.0f;
				if (!!(linedef->flags & ML_DONTPEGBOTTOM) ^ !!(linedef->flags & ML_EFFECT3))
					rw_midtexturemid = rw_midtextureback = FIXED_TO_FLOAT(max(frontsector->floorheight, backsector->floorheight) - viewz);
				else
					rw_midtexturemid = rw_midtextureback = FIXED_TO_FLOAT(min(frontsector->ceilingheight, backsector->ceilingheight) - viewz);
			}
			else if (!!(linedef->flags & ML_DONTPEGBOTTOM) ^ !!(linedef->flags & ML_EFFECT3))
			{
				rw_midtexturemid = worldbottom;
				rw_midtextureslide = FIXED_TO_FLOAT(floorfrontslide);
				rw_midtextureback = worldlow;
				rw_midtexturebackslide = FIXED_TO_FLOAT(floorbackslide);
			}
			else
			{
				rw_midtexturemid = worldtop;
				rw_midtextureslide = FIXED_TO_FLOAT(ceilingfrontslide);
				rw_midtextureback = worldhigh;
				rw_midtexturebackslide = FIXED_TO_FLOAT(ceilingbackslide);
			}

			rw_midtexturemid += FIXED_TO_FLOAT(sidedef->rowoffset);
			rw_midtextureback += FIXED_TO_FLOAT(sidedef->rowoffset);

			maskedtexture = true;
		}
	}

	// calculate rw_offset (only needed for textured lines)
	segtextured = midtexture || toptexture || bottomtexture || maskedtexture || (numthicksides > 0);

	if (segtextured)
	{
		offsetangle = rw_normalangle-rw_angle1;

		if (offsetangle > ANGLE_180)
			offsetangle = -(signed)offsetangle;

		if (offsetangle > ANGLE_90)
			offsetangle = ANGLE_90;

		sineval = FINESINE(offsetangle>>ANGLETOFINESHIFT);
		// big room fix
		if (overflow)
			rw_offset = R_CalcSegDistFloat(curline, FIXED_TO_FLOAT(viewx), FIXED_TO_FLOAT(viewy), true);
		else
			rw_offset = FIXED_TO_FLOAT(hyp) * FIXED_TO_FLOAT(sineval);

		if (rw_normalangle-rw_angle1 < ANGLE_180)
			rw_offset = -rw_offset;

		/// don't use texture offset for splats
		rw_offset2 = rw_offset + FIXED_TO_FLOAT(curline->offset);
		rw_offset += FIXED_TO_FLOAT(sidedef->textureoffset + curline->offset);
		rw_centerangle = ANGLE_90 + viewangle - rw_normalangle;

		// calculate light table
		//  use different light tables
		//  for horizontal / vertical / diagonal
		// OPTIMIZE: get rid of LIGHTSEGSHIFT globally
		lightnum = (frontsector->lightlevel >> LIGHTSEGSHIFT);

		if (curline->v1->y == curline->v2->y)
			lightnum--;
		else if (curline->v1->x == curline->v2->x)
			lightnum++;

		if (lightnum < 0)
			walllights = scalelight[0];
		else if (lightnum >= LIGHTLEVELS)
			walllights = scalelight[LIGHTLEVELS - 1];
		else
			walllights = scalelight[lightnum];
	}

	// if a floor / ceiling plane is on the wrong side
	//  of the view plane, it is definitely invisible
	//  and doesn't need to be marked.
	if (frontsector->heightsec == -1)
	{
		if (frontsector->floorpic != skyflatnum && (frontsector->f_slope ?
			P_GetZAt(frontsector->f_slope, viewx, viewy) :
			frontsector->floorheight) >= viewz)
		{
			// above view plane
			markfloor = false;
		}

		if (frontsector->ceilingpic != skyflatnum && (frontsector->c_slope ?
			P_GetZAt(frontsector->c_slope, viewx, viewy) :
			frontsector->ceilingheight) <= viewz)
		{
			// below view plane
			markceiling = false;
		}
	}

	// calculate incremental stepping values for texture edges
	if (linedef->special == HORIZONSPECIAL) { // HORIZON LINES
		topstep = bottomstep = 0.0f;
		topfrac = bottomfrac = centeryfloat;
		topfrac++; // Prevent 1px HOM
	} else {
		topstep = -(rw_scalestep*worldtop);
		topfrac = centeryfloat - (worldtop*rw_scale);

		bottomstep = -(rw_scalestep*worldbottom);
		bottomfrac = centeryfloat - (worldbottom*rw_scale);

		if (frontsector->c_slope) {
			float topfracend = centeryfloat - (worldtopslope*ds_p->scale2);
			topstep = (topfracend-topfrac)/(range);
		}
		if (frontsector->f_slope) {
			float bottomfracend = centeryfloat - (worldbottomslope*ds_p->scale2);
			bottomstep = (bottomfracend-bottomfrac)/(range);
		}
	}

	dc_numlights = 0;

	if (frontsector->numlights)
	{
		dc_numlights = frontsector->numlights;
		if (dc_numlights >= dc_maxlights)
		{
			dc_maxlights = dc_numlights;
			dc_lightlist = Z_Realloc(dc_lightlist, sizeof (*dc_lightlist) * dc_maxlights, PU_STATIC, NULL);
		}

		for (i = p = 0; i < dc_numlights; i++)
		{
			float leftheight, rightheight;

			light = &frontsector->lightlist[i];
			rlight = &dc_lightlist[p];

			if (light->slope) {
				fixed_t left = P_GetZAt(light->slope, segleft.x, segleft.y);
				fixed_t right = P_GetZAt(light->slope, segright.x, segright.y);
				leftheight = FIXED_TO_FLOAT(left);
				rightheight = FIXED_TO_FLOAT(right);

				// Flag sector as having slopes
				frontsector->hasslope = true;
			} else
				leftheight = rightheight = FIXED_TO_FLOAT(light->height);

			leftheight -= FIXED_TO_FLOAT(viewz);
			rightheight -= FIXED_TO_FLOAT(viewz);

			if (i != 0)
			{
				if (leftheight < worldbottom && rightheight < worldbottomslope)
					continue;

				if (leftheight > worldtop && rightheight > worldtopslope && i+1 < dc_numlights && frontsector->lightlist[i+1].height > frontsector->ceilingheight)
					continue;
			}

			rlight->height = FLOAT_TO_FIXED(centeryfloat - (leftheight*rw_scale));
			rlight->heightstep = FLOAT_TO_FIXED(centeryfloat - (rightheight*ds_p->scale2));
			rlight->heightstep = (rlight->heightstep-rlight->height)/(range);
			rlight->flags = light->flags;

			if (light->caster && light->caster->flags & FF_CUTSOLIDS)
			{
				if (*light->caster->b_slope) {
					fixed_t left = P_GetZAt(*light->caster->b_slope, segleft.x, segleft.y);
					fixed_t right = P_GetZAt(*light->caster->b_slope, segright.x, segright.y);
					leftheight = FIXED_TO_FLOAT(left);
					rightheight = FIXED_TO_FLOAT(right);

					// Flag sector as having slopes
					frontsector->hasslope = true;
				} else
					leftheight = rightheight = FIXED_TO_FLOAT(*light->caster->bottomheight);

				leftheight -= FIXED_TO_FLOAT(viewz);
				rightheight -= FIXED_TO_FLOAT(viewz);

				//leftheight >>= 4;
				//rightheight >>= 4;

				rlight->botheight = FLOAT_TO_FIXED(centeryfloat - (leftheight*rw_scale));
				rlight->botheightstep = FLOAT_TO_FIXED(centeryfloat - (rightheight*ds_p->scale2));
				rlight->botheightstep = (rlight->botheightstep-rlight->botheight)/(range);
			}

			rlight->lightlevel = *light->lightlevel;
			rlight->extra_colormap = *light->extra_colormap;
			p++;
		}

		dc_numlights = p;
	}

	if (numffloors)
	{
		for (i = 0; i < numffloors; i++)
		{
			if (linedef->special == HORIZONSPECIAL) // Horizon lines extend FOFs in contact with them too.
			{
				ffloor[i].f_step = 0.0f;
				ffloor[i].f_frac = centeryfloat;
				topfrac++; // Prevent 1px HOM
			}
			else
			{
				ffloor[i].f_frac = centeryfloat - (ffloor[i].f_pos*rw_scale);
				ffloor[i].f_step = (centeryfloat - (ffloor[i].f_pos_slope*ds_p->scale2) - ffloor[i].f_frac)/(range);
			}
		}
	}

	if (backsector)
	{
		if (toptexture)
		{
			pixhigh = centeryfloat - (worldhigh*rw_scale);
			pixhighstep = -(rw_scalestep*worldhigh);

			if (backsector->c_slope) {
				float topfracend = centeryfloat - (worldhighslope*ds_p->scale2);
				pixhighstep = (topfracend-pixhigh)/(range);
			}
		}

		if (bottomtexture)
		{
			pixlow = centeryfloat - (worldlow*rw_scale);
			pixlowstep = -(rw_scalestep*worldlow);

			if (backsector->f_slope) {
				float bottomfracend = centeryfloat - (worldlowslope*ds_p->scale2);
				pixlowstep = (bottomfracend-pixlow)/(range);
			}
		}

		{
			ffloor_t * rover;
			float roverleft, roverright;
			fixed_t roverleftfixed = 0, roverrightfixed = 0;
			fixed_t planevistest;
			i = 0;

			if (backsector->ffloors)
			{
				for (rover = backsector->ffloors; rover && i < MAXFFLOORS; rover = rover->next)
				{
					if (!(rover->flags & FF_EXISTS) || !(rover->flags & FF_RENDERPLANES))
						continue;
					if (rover->norender == leveltime)
						continue;

					// Let the renderer know this sector is sloped.
					if (*rover->b_slope || *rover->t_slope)
						backsector->hasslope = true;

					roverleftfixed = (*rover->b_slope ? P_GetZAt(*rover->b_slope, segleft.x, segleft.y) : *rover->bottomheight) - viewz;
					roverrightfixed = (*rover->b_slope ? P_GetZAt(*rover->b_slope, segright.x, segright.y) : *rover->bottomheight) - viewz;
					planevistest = (*rover->b_slope ? P_GetZAt(*rover->b_slope, viewx, viewy) : *rover->bottomheight);

					roverleft = FIXED_TO_FLOAT(roverleftfixed);
					roverright = FIXED_TO_FLOAT(roverrightfixed);

					if ((roverleftfixed <= worldhighfixed || roverrightfixed <= worldhighslopefixed) &&
					    (roverleftfixed >= worldlowfixed || roverrightfixed >= worldlowslopefixed) &&
					    ((viewz < planevistest && (rover->flags & FF_BOTHPLANES || !(rover->flags & FF_INVERTPLANES))) ||
					     (viewz > planevistest && (rover->flags & FF_BOTHPLANES || rover->flags & FF_INVERTPLANES))))
					{
						//ffloor[i].slope = *rover->b_slope;
						ffloor[i].b_pos = roverleft;
						ffloor[i].b_pos_slope = roverright;
						//ffloor[i].b_pos >>= 4;
						//ffloor[i].b_pos_slope >>= 4;
						ffloor[i].b_frac = centeryfloat - (ffloor[i].b_pos*rw_scale);
						ffloor[i].b_step = centeryfloat - (ffloor[i].b_pos_slope*ds_p->scale2);
						ffloor[i].b_step = (ffloor[i].b_step-ffloor[i].b_frac)/(range);
						i++;
					}

					if (i >= MAXFFLOORS)
						break;

					roverleftfixed = (*rover->t_slope ? P_GetZAt(*rover->t_slope, segleft.x, segleft.y) : *rover->topheight) - viewz;
					roverrightfixed = (*rover->t_slope ? P_GetZAt(*rover->t_slope, segright.x, segright.y) : *rover->topheight) - viewz;
					planevistest = (*rover->t_slope ? P_GetZAt(*rover->t_slope, viewx, viewy) : *rover->topheight);

					roverleft = FIXED_TO_FLOAT(roverleftfixed);
					roverright = FIXED_TO_FLOAT(roverrightfixed);

					if ((roverleftfixed <= worldhighfixed || roverrightfixed <= worldhighslopefixed) &&
					    (roverleftfixed >= worldlowfixed || roverrightfixed >= worldlowslopefixed) &&
						((viewz > planevistest && (rover->flags & FF_BOTHPLANES || !(rover->flags & FF_INVERTPLANES))) ||
					     (viewz < planevistest && (rover->flags & FF_BOTHPLANES || rover->flags & FF_INVERTPLANES))))
					{
						//ffloor[i].slope = *rover->t_slope;
						ffloor[i].b_pos = roverleft;
						ffloor[i].b_pos_slope = roverright;
						//ffloor[i].b_pos >>= 4;
						//ffloor[i].b_pos_slope >>= 4;
						ffloor[i].b_frac = centeryfloat - (ffloor[i].b_pos*rw_scale);
						ffloor[i].b_step = centeryfloat - (ffloor[i].b_pos_slope*ds_p->scale2);
						ffloor[i].b_step = (ffloor[i].b_step-ffloor[i].b_frac)/(range);
						i++;
					}
				}
			}
			else if (frontsector && frontsector->ffloors)
			{
				for (rover = frontsector->ffloors; rover && i < MAXFFLOORS; rover = rover->next)
				{
					if (!(rover->flags & FF_EXISTS) || !(rover->flags & FF_RENDERPLANES))
						continue;
					if (rover->norender == leveltime)
						continue;

					// Let the renderer know this sector is sloped.
					if (*rover->b_slope || *rover->t_slope)
						frontsector->hasslope = true;

					roverleftfixed = (*rover->b_slope ? P_GetZAt(*rover->b_slope, segleft.x, segleft.y) : *rover->bottomheight) - viewz;
					roverrightfixed = (*rover->b_slope ? P_GetZAt(*rover->b_slope, segright.x, segright.y) : *rover->bottomheight) - viewz;
					planevistest = (*rover->b_slope ? P_GetZAt(*rover->b_slope, viewx, viewy) : *rover->bottomheight);

					roverleft = FIXED_TO_FLOAT(roverleftfixed);
					roverright = FIXED_TO_FLOAT(roverrightfixed);

					if ((roverleftfixed <= worldhighfixed || roverrightfixed <= worldhighslopefixed) &&
					    (roverleftfixed >= worldlowfixed || roverrightfixed >= worldlowslopefixed) &&
					    ((viewz < planevistest && (rover->flags & FF_BOTHPLANES || !(rover->flags & FF_INVERTPLANES))) ||
					     (viewz > planevistest && (rover->flags & FF_BOTHPLANES || rover->flags & FF_INVERTPLANES))))
					{
						//ffloor[i].slope = *rover->b_slope;
						ffloor[i].b_pos = roverleft;
						ffloor[i].b_pos_slope = roverright;
						//ffloor[i].b_pos >>= 4;
						//ffloor[i].b_pos_slope >>= 4;
						ffloor[i].b_frac = centeryfloat - (ffloor[i].b_pos*rw_scale);
						ffloor[i].b_step = centeryfloat - (ffloor[i].b_pos_slope*ds_p->scale2);
						ffloor[i].b_step = (ffloor[i].b_step-ffloor[i].b_frac)/(range);
						i++;
					}

					if (i >= MAXFFLOORS)
						break;

					roverleftfixed = (*rover->t_slope ? P_GetZAt(*rover->t_slope, segleft.x, segleft.y) : *rover->topheight) - viewz;
					roverrightfixed = (*rover->t_slope ? P_GetZAt(*rover->t_slope, segright.x, segright.y) : *rover->topheight) - viewz;
					planevistest = (*rover->t_slope ? P_GetZAt(*rover->t_slope, viewx, viewy) : *rover->topheight);

					roverleft = FIXED_TO_FLOAT(roverleftfixed);
					roverright = FIXED_TO_FLOAT(roverrightfixed);

					if ((roverleftfixed <= worldhighfixed || roverrightfixed <= worldhighslopefixed) &&
					    (roverleftfixed >= worldlowfixed || roverrightfixed >= worldlowslopefixed) &&
					    ((viewz > planevistest && (rover->flags & FF_BOTHPLANES || !(rover->flags & FF_INVERTPLANES))) ||
					     (viewz < planevistest && (rover->flags & FF_BOTHPLANES || rover->flags & FF_INVERTPLANES))))
					{
						//ffloor[i].slope = *rover->t_slope;
						ffloor[i].b_pos = roverleft;
						ffloor[i].b_pos_slope = roverright;
						//ffloor[i].b_pos >>= 4;
						//ffloor[i].b_pos_slope >>= 4;
						ffloor[i].b_frac = centeryfloat - (ffloor[i].b_pos*rw_scale);
						ffloor[i].b_step = centeryfloat - (ffloor[i].b_pos_slope*ds_p->scale2);
						ffloor[i].b_step = (ffloor[i].b_step-ffloor[i].b_frac)/(range);
						i++;
					}
				}
			}
			if (curline->polyseg && frontsector && (curline->polyseg->flags & POF_RENDERPLANES))
			{
				while (i < numffloors && ffloor[i].polyobj != curline->polyseg) i++;
				if (i < numffloors && backsector->floorheight <= frontsector->ceilingheight &&
					backsector->floorheight >= frontsector->floorheight &&
					(viewz < backsector->floorheight))
				{
					if (ffloor[i].plane->minx > ds_p->x1)
						ffloor[i].plane->minx = ds_p->x1;

					if (ffloor[i].plane->maxx < ds_p->x2)
						ffloor[i].plane->maxx = ds_p->x2;

					ffloor[i].slope = NULL;
					ffloor[i].b_pos = FIXED_TO_FLOAT(backsector->floorheight - viewz);
					ffloor[i].b_step = (-rw_scalestep*ffloor[i].b_pos);
					ffloor[i].b_frac = centeryfloat - (ffloor[i].b_pos*rw_scale);
					i++;
				}
				if (i < numffloors && backsector->ceilingheight >= frontsector->floorheight &&
					backsector->ceilingheight <= frontsector->ceilingheight &&
					(viewz > backsector->ceilingheight))
				{
					if (ffloor[i].plane->minx > ds_p->x1)
						ffloor[i].plane->minx = ds_p->x1;

					if (ffloor[i].plane->maxx < ds_p->x2)
						ffloor[i].plane->maxx = ds_p->x2;

					ffloor[i].slope = NULL;
					ffloor[i].b_pos = FIXED_TO_FLOAT(backsector->ceilingheight - viewz);
					ffloor[i].b_step = (-rw_scalestep*ffloor[i].b_pos);
					ffloor[i].b_frac = centeryfloat - (ffloor[i].b_pos*rw_scale);
					i++;
				}
			}

			numbackffloors = i;
		}
	}

	// get a new or use the same visplane
	if (markceiling)
	{
		if (ceilingplane) //SoM: 3/29/2000: Check for null ceiling planes
			ceilingplane = R_CheckPlane (ceilingplane, rw_x, rw_stopx-1);
		else
			markceiling = false;

		// Don't mark ceiling flat lines for polys unless this line has an upper texture, otherwise we get flat leakage pulling downward
		// (If it DOES have an upper texture and we do this, the ceiling won't render at all)
		if (curline->polyseg && !curline->sidedef->toptexture)
			markceiling = false;
	}

	// get a new or use the same visplane
	if (markfloor)
	{
		if (floorplane) //SoM: 3/29/2000: Check for null planes
			floorplane = R_CheckPlane (floorplane, rw_x, rw_stopx-1);
		else
			markfloor = false;

		// Don't mark floor flat lines for polys unless this line has a lower texture, otherwise we get flat leakage pulling upward
		// (If it DOES have a lower texture and we do this, the floor won't render at all)
		if (curline->polyseg && !curline->sidedef->bottomtexture)
			markfloor = false;
	}

	ds_p->numffloorplanes = 0;
	if (numffloors)
	{
		if (!firstseg)
		{
			ds_p->numffloorplanes = numffloors;

			for (i = 0; i < numffloors; i++)
			{
				ds_p->ffloorplanes[i] = ffloor[i].plane =
					R_CheckPlane(ffloor[i].plane, rw_x, rw_stopx - 1);
			}

			firstseg = ds_p;
		}
		else
		{
			for (i = 0; i < numffloors; i++)
				R_ExpandPlane(ffloor[i].plane, rw_x, rw_stopx - 1);
		}
		// FIXME hack to fix planes disappearing when a seg goes behind the camera. This NEEDS to be changed to be done properly. -Red
		if (curline->polyseg)
		{
			for (i = 0; i < numffloors; i++)
			{
				if (!ffloor[i].polyobj || ffloor[i].polyobj != curline->polyseg)
					continue;
				if (ffloor[i].plane->minx > rw_x)
					ffloor[i].plane->minx = rw_x;

				if (ffloor[i].plane->maxx < rw_stopx - 1)
					ffloor[i].plane->maxx = rw_stopx - 1;
			}
		}
	}

#ifdef WALLSPLATS
	if (linedef->splats && cv_splats.value)
	{
		// Isn't a bit wasteful to copy the ENTIRE array for every drawseg?
		M_Memcpy(last_ceilingclip + ds_p->x1, ceilingclip + ds_p->x1,
			sizeof (INT16) * (ds_p->x2 - ds_p->x1 + 1));
		M_Memcpy(last_floorclip + ds_p->x1, floorclip + ds_p->x1,
			sizeof (INT16) * (ds_p->x2 - ds_p->x1 + 1));
		R_RenderSegLoop();
		R_DrawWallSplats();
	}
	else
#endif
		R_RenderSegLoop();
	colfunc = colfuncs[BASEDRAWFUNC];

	if (portalline) // if curline is a portal, set portalrender for drawseg
		ds_p->portalpass = portalrender+1;
	else
		ds_p->portalpass = 0;

	// save sprite clipping info
	if (((ds_p->silhouette & SIL_TOP) || maskedtexture) && !ds_p->sprtopclip)
	{
		M_Memcpy(lastopening, ceilingclip+start, 2*(rw_stopx - start));
		ds_p->sprtopclip = lastopening - start;
		lastopening += rw_stopx - start;
	}

	if (((ds_p->silhouette & SIL_BOTTOM) || maskedtexture) && !ds_p->sprbottomclip)
	{
		M_Memcpy(lastopening, floorclip + start, 2*(rw_stopx-start));
		ds_p->sprbottomclip = lastopening - start;
		lastopening += rw_stopx - start;
	}

	if (maskedtexture && !(ds_p->silhouette & SIL_TOP))
	{
		ds_p->silhouette |= SIL_TOP;
		ds_p->tsilheight = (sidedef->midtexture > 0 && sidedef->midtexture < numtextures) ? INT32_MIN: INT32_MAX;
	}
	if (maskedtexture && !(ds_p->silhouette & SIL_BOTTOM))
	{
		ds_p->silhouette |= SIL_BOTTOM;
		ds_p->bsilheight = (sidedef->midtexture > 0 && sidedef->midtexture < numtextures) ? INT32_MAX: INT32_MIN;
	}
	ds_p++;
}
