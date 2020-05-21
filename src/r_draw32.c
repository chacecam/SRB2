// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  r_draw32.c
/// \brief 32bpp span/column drawer functions
/// \note  no includes because this is included as part of r_draw.c

// ==========================================================================
// COLUMNS
// ==========================================================================

// A column is a vertical slice/span of a wall texture that uses
// a has a constant z depth from top to bottom.
//

/**	\brief The R_DrawColumn_32 function
	Experiment to make software go faster. Taken from the Boom source
*/
void R_DrawColumn_32(void)
{
	INT32 count;
	register UINT32 *dest;
	register fixed_t frac;
	fixed_t fracstep;

	count = dc_yh - dc_yl;

	if (count < 0) // Zero length, column does not exceed a pixel.
		return;

#ifdef RANGECHECK
	if ((unsigned)dc_x >= (unsigned)vid.width || dc_yl < 0 || dc_yh >= vid.height)
		return;
#endif

	// Framebuffer destination address.
	// Use ylookup LUT to avoid multiply with ScreenWidth.
	// Use columnofs LUT for subwindows?

	//dest = ylookup[dc_yl] + columnofs[dc_x];
	dest = &topleft_u32[dc_yl*vid.width + dc_x];

	count++;

	// Determine scaling, which is the only mapping to be done.
	fracstep = dc_iscale;
	//frac = dc_texturemid + (dc_yl - centery)*fracstep;
	frac = (dc_texturemid + FixedMul((dc_yl << FRACBITS) - centeryfrac, fracstep))*(!dc_hires);

	// Inner loop that does the actual texture mapping, e.g. a DDA-like scaling.
	// This is as fast as it gets.
	{
		register const UINT8 *source = dc_source;
		register const UINT32 *sourceu32 = (UINT32 *)dc_source;
		register lighttable_t *colormap = dc_colormap;
		register lighttable_u32_t *colormapu32 = NULL;
		register INT32 heightmask = dc_texheight-1;
		if (dc_texheight & heightmask)   // not a power of 2 -- killough
		{
			heightmask++;
			heightmask <<= FRACBITS;

			if (frac < 0)
				while ((frac += heightmask) <  0);
			else
				while (frac >= heightmask)
					frac -= heightmask;

			if (dc_picfmt == PICFMT_PATCH)
			{
				if (dc_colmapstyle == TC_COLORMAPSTYLE_8BPP)
				{
					do
					{
						// Re-map color indices from wall texture column
						//  using a lighting/special effects LUT.
						// heightmask is the Tutti-Frutti fix
						*dest = GetTrueColor(colormap[source[frac>>FRACBITS]]);
						dest += vid.width;

						// Avoid overflow.
						if (fracstep > 0x7FFFFFFF - frac)
							frac += fracstep - heightmask;
						else
							frac += fracstep;

						while (frac >= heightmask)
							frac -= heightmask;
					} while (--count);
				}
				else if (dc_colmapstyle == TC_COLORMAPSTYLE_32BPP)
				{
					colormapu32 = (lighttable_u32_t *)colormap;
					do
					{
						// Re-map color indices from wall texture column
						//  using a lighting/special effects LUT.
						// heightmask is the Tutti-Frutti fix
						*dest = (colormapu32[source[frac>>FRACBITS]]);
						dest += vid.width;

						// Avoid overflow.
						if (fracstep > 0x7FFFFFFF - frac)
							frac += fracstep - heightmask;
						else
							frac += fracstep;

						while (frac >= heightmask)
							frac -= heightmask;
					} while (--count);
				}
			}
			else if (dc_picfmt == PICFMT_PATCH32)
			{
				do
				{
					// Re-map color indices from wall texture column
					//  using a lighting/special effects LUT.
					// heightmask is the Tutti-Frutti fix
					*dest = TC_ColorMix(sourceu32[frac>>FRACBITS], *dest);
					dest += vid.width;

					// Avoid overflow.
					if (fracstep > 0x7FFFFFFF - frac)
						frac += fracstep - heightmask;
					else
						frac += fracstep;

					while (frac >= heightmask)
						frac -= heightmask;
				} while (--count);
			}
		}
		else
		{
			if (dc_picfmt == PICFMT_PATCH)
			{
				if (dc_colmapstyle == TC_COLORMAPSTYLE_8BPP)
				{
					while ((count -= 2) >= 0) // texture height is a power of 2
					{
						*dest = GetTrueColor(colormap[source[(frac>>FRACBITS) & heightmask]]);
						dest += vid.width;
						frac += fracstep;
						*dest = GetTrueColor(colormap[source[(frac>>FRACBITS) & heightmask]]);
						dest += vid.width;
						frac += fracstep;
					}
					if (count & 1)
						*dest = GetTrueColor(colormap[source[(frac>>FRACBITS) & heightmask]]);
				}
				else if (dc_colmapstyle == TC_COLORMAPSTYLE_32BPP)
				{
					colormapu32 = (lighttable_u32_t *)colormap;
					while ((count -= 2) >= 0) // texture height is a power of 2
					{
						*dest = (colormapu32[source[(frac>>FRACBITS) & heightmask]]);
						dest += vid.width;
						frac += fracstep;
						*dest = (colormapu32[source[(frac>>FRACBITS) & heightmask]]);
						dest += vid.width;
						frac += fracstep;
					}
					if (count & 1)
						*dest = (colormapu32[source[(frac>>FRACBITS) & heightmask]]);
				}
			}
			else if (dc_picfmt == PICFMT_PATCH32)
			{
				while ((count -= 2) >= 0) // texture height is a power of 2
				{
					*dest = TC_ColorMix(sourceu32[(frac>>FRACBITS) & heightmask], *dest);
					dest += vid.width;
					frac += fracstep;
					*dest = TC_ColorMix(sourceu32[(frac>>FRACBITS) & heightmask], *dest);
					dest += vid.width;
					frac += fracstep;
				}
				if (count & 1)
					*dest = TC_ColorMix(sourceu32[(frac>>FRACBITS) & heightmask], *dest);
			}
		}
	}
}

void R_Draw2sMultiPatchColumn_32(void)
{
	INT32 count;
	register UINT32 *dest;
	register fixed_t frac;
	fixed_t fracstep;

	count = dc_yh - dc_yl;

	if (count < 0) // Zero length, column does not exceed a pixel.
		return;

#ifdef RANGECHECK
	if ((unsigned)dc_x >= (unsigned)vid.width || dc_yl < 0 || dc_yh >= vid.height)
		return;
#endif

	// Framebuffer destination address.
	// Use ylookup LUT to avoid multiply with ScreenWidth.
	// Use columnofs LUT for subwindows?

	//dest = ylookup[dc_yl] + columnofs[dc_x];
	dest = &topleft_u32[dc_yl*vid.width + dc_x];

	count++;

	// Determine scaling, which is the only mapping to be done.
	fracstep = dc_iscale;
	//frac = dc_texturemid + (dc_yl - centery)*fracstep;
	frac = (dc_texturemid + FixedMul((dc_yl << FRACBITS) - centeryfrac, fracstep))*(!dc_hires);

	// Inner loop that does the actual texture mapping, e.g. a DDA-like scaling.
	// This is as fast as it gets.
	{
		register const UINT8 *source = dc_source;
		register const UINT32 *sourceu32 = (UINT32 *)dc_source;
		register lighttable_t *colormap = dc_colormap;
		register lighttable_u32_t *colormapu32 = NULL;
		register INT32 heightmask = dc_texheight-1;
		register UINT8 val;
		register UINT32 valu32;
		if (dc_texheight & heightmask)   // not a power of 2 -- killough
		{
			heightmask++;
			heightmask <<= FRACBITS;

			if (frac < 0)
				while ((frac += heightmask) <  0);
			else
				while (frac >= heightmask)
					frac -= heightmask;

			if (dc_picfmt == PICFMT_PATCH)
			{
				if (dc_colmapstyle == TC_COLORMAPSTYLE_8BPP)
				{
					do
					{
						// Re-map color indices from wall texture column
						//  using a lighting/special effects LUT.
						// heightmask is the Tutti-Frutti fix
						val = source[frac>>FRACBITS];

						if (val != TRANSPARENTPIXEL)
							*dest = GetTrueColor(colormap[val]);

						dest += vid.width;

						// Avoid overflow.
						if (fracstep > 0x7FFFFFFF - frac)
							frac += fracstep - heightmask;
						else
							frac += fracstep;

						while (frac >= heightmask)
							frac -= heightmask;
					} while (--count);
				}
				else if (dc_colmapstyle == TC_COLORMAPSTYLE_32BPP)
				{
					colormapu32 = (lighttable_u32_t *)colormap;
					do
					{
						// Re-map color indices from wall texture column
						//  using a lighting/special effects LUT.
						// heightmask is the Tutti-Frutti fix
						val = source[frac>>FRACBITS];

						if (val != TRANSPARENTPIXEL)
							*dest = (colormapu32[val]);

						dest += vid.width;

						// Avoid overflow.
						if (fracstep > 0x7FFFFFFF - frac)
							frac += fracstep - heightmask;
						else
							frac += fracstep;

						while (frac >= heightmask)
							frac -= heightmask;
					} while (--count);
				}
			}
			else if (dc_picfmt == PICFMT_PATCH32)
			{
				do
				{
					// Re-map color indices from wall texture column
					//  using a lighting/special effects LUT.
					// heightmask is the Tutti-Frutti fix
					valu32 = sourceu32[frac>>FRACBITS];

					if (R_GetRgbaA(valu32))
						*dest = TC_ColorMix(valu32, *dest);

					dest += vid.width;

					// Avoid overflow.
					if (fracstep > 0x7FFFFFFF - frac)
						frac += fracstep - heightmask;
					else
						frac += fracstep;

					while (frac >= heightmask)
						frac -= heightmask;
				} while (--count);
			}
		}
		else
		{
			if (dc_picfmt == PICFMT_PATCH)
			{
				if (dc_colmapstyle == TC_COLORMAPSTYLE_8BPP)
				{
					while ((count -= 2) >= 0) // texture height is a power of 2
					{
						val = source[(frac>>FRACBITS) & heightmask];
						if (val != TRANSPARENTPIXEL)
							*dest = GetTrueColor(colormap[val]);
						dest += vid.width;
						frac += fracstep;
						val = source[(frac>>FRACBITS) & heightmask];
						if (val != TRANSPARENTPIXEL)
							*dest = GetTrueColor(colormap[val]);
						dest += vid.width;
						frac += fracstep;
					}
					if (count & 1)
					{
						val = source[(frac>>FRACBITS) & heightmask];
						if (val != TRANSPARENTPIXEL)
							*dest = GetTrueColor(colormap[val]);
					}
				}
				else if (dc_colmapstyle == TC_COLORMAPSTYLE_32BPP)
				{
					colormapu32 = (lighttable_u32_t *)colormap;
					while ((count -= 2) >= 0) // texture height is a power of 2
					{
						val = source[(frac>>FRACBITS) & heightmask];
						if (val != TRANSPARENTPIXEL)
							*dest = (colormapu32[val]);
						dest += vid.width;
						frac += fracstep;
						val = source[(frac>>FRACBITS) & heightmask];
						if (val != TRANSPARENTPIXEL)
							*dest = (colormapu32[val]);
						dest += vid.width;
						frac += fracstep;
					}
					if (count & 1)
					{
						val = source[(frac>>FRACBITS) & heightmask];
						if (val != TRANSPARENTPIXEL)
							*dest = (colormapu32[val]);
					}
				}
			}
			else if (dc_picfmt == PICFMT_PATCH32)
			{
				while ((count -= 2) >= 0) // texture height is a power of 2
				{
					valu32 = sourceu32[(frac>>FRACBITS) & heightmask];
					if (R_GetRgbaA(valu32))
						*dest = TC_ColorMix(valu32, *dest);
					dest += vid.width;
					frac += fracstep;
					valu32 = sourceu32[(frac>>FRACBITS) & heightmask];
					if (R_GetRgbaA(valu32))
						*dest = TC_ColorMix(valu32, *dest);
					dest += vid.width;
					frac += fracstep;
				}
				if (count & 1)
				{
					valu32 = sourceu32[(frac>>FRACBITS) & heightmask];
					if (R_GetRgbaA(valu32))
						*dest = TC_ColorMix(valu32, *dest);
				}
			}
		}
	}
}

void R_Draw2sMultiPatchTranslucentColumn_32(void)
{
	INT32 count;
	register UINT32 *dest;
	register fixed_t frac;
	fixed_t fracstep;

	count = dc_yh - dc_yl;

	if (count < 0) // Zero length, column does not exceed a pixel.
		return;

#ifdef RANGECHECK
	if ((unsigned)dc_x >= (unsigned)vid.width || dc_yl < 0 || dc_yh >= vid.height)
		return;
#endif

	// Framebuffer destination address.
	// Use ylookup LUT to avoid multiply with ScreenWidth.
	// Use columnofs LUT for subwindows?

	//dest = ylookup[dc_yl] + columnofs[dc_x];
	dest = &topleft_u32[dc_yl*vid.width + dc_x];

	count++;

	// Determine scaling, which is the only mapping to be done.
	fracstep = dc_iscale;
	//frac = dc_texturemid + (dc_yl - centery)*fracstep;
	frac = (dc_texturemid + FixedMul((dc_yl << FRACBITS) - centeryfrac, fracstep))*(!dc_hires);

	// Inner loop that does the actual texture mapping, e.g. a DDA-like scaling.
	// This is as fast as it gets.
	{
		register const UINT8 *source = dc_source;
		register const UINT32 *sourceu32 = (UINT32 *)dc_source;
		register lighttable_t *colormap = dc_colormap;
		register lighttable_u32_t *colormapu32 = NULL;
		register INT32 heightmask = dc_texheight-1;
		register UINT8 val;
		register UINT32 valu32;
		if (dc_texheight & heightmask)   // not a power of 2 -- killough
		{
			heightmask++;
			heightmask <<= FRACBITS;

			if (frac < 0)
				while ((frac += heightmask) <  0);
			else
				while (frac >= heightmask)
					frac -= heightmask;

			if (dc_picfmt == PICFMT_PATCH)
			{
				if (dc_colmapstyle == TC_COLORMAPSTYLE_8BPP)
				{
					do
					{
						// Re-map color indices from wall texture column
						//  using a lighting/special effects LUT.
						// heightmask is the Tutti-Frutti fix
						val = source[frac>>FRACBITS];

						if (val != TRANSPARENTPIXEL)
							WriteTranslucentColumn(colormap[val]);

						dest += vid.width;

						// Avoid overflow.
						if (fracstep > 0x7FFFFFFF - frac)
							frac += fracstep - heightmask;
						else
							frac += fracstep;

						while (frac >= heightmask)
							frac -= heightmask;
					} while (--count);
				}
				else if (dc_colmapstyle == TC_COLORMAPSTYLE_32BPP)
				{
					colormapu32 = (lighttable_u32_t *)colormap;
					do
					{
						// Re-map color indices from wall texture column
						//  using a lighting/special effects LUT.
						// heightmask is the Tutti-Frutti fix
						val = source[frac>>FRACBITS];

						if (val != TRANSPARENTPIXEL)
							WriteTranslucentColumn32(colormapu32[val]);

						dest += vid.width;

						// Avoid overflow.
						if (fracstep > 0x7FFFFFFF - frac)
							frac += fracstep - heightmask;
						else
							frac += fracstep;

						while (frac >= heightmask)
							frac -= heightmask;
					} while (--count);
				}
			}
			else if (dc_picfmt == PICFMT_PATCH32)
			{
				do
				{
					// Re-map color indices from wall texture column
					//  using a lighting/special effects LUT.
					// heightmask is the Tutti-Frutti fix
					valu32 = sourceu32[frac>>FRACBITS];

					if (R_GetRgbaA(valu32))
						*dest = TC_TranslucentColorMix(valu32, *dest, dc_alpha);

					dest += vid.width;

					// Avoid overflow.
					if (fracstep > 0x7FFFFFFF - frac)
						frac += fracstep - heightmask;
					else
						frac += fracstep;

					while (frac >= heightmask)
						frac -= heightmask;
				} while (--count);
			}
		}
		else
		{
			if (dc_picfmt == PICFMT_PATCH)
			{
				if (dc_colmapstyle == TC_COLORMAPSTYLE_8BPP)
				{
					while ((count -= 2) >= 0) // texture height is a power of 2
					{
						val = source[(frac>>FRACBITS) & heightmask];
						if (val != TRANSPARENTPIXEL)
							WriteTranslucentColumn(colormap[val]);
						dest += vid.width;
						frac += fracstep;
						val = source[(frac>>FRACBITS) & heightmask];
						if (val != TRANSPARENTPIXEL)
							WriteTranslucentColumn(colormap[val]);
						dest += vid.width;
						frac += fracstep;
					}
					if (count & 1)
					{
						val = source[(frac>>FRACBITS) & heightmask];
						if (val != TRANSPARENTPIXEL)
							WriteTranslucentColumn(colormap[val]);
					}
				}
				else if (dc_colmapstyle == TC_COLORMAPSTYLE_32BPP)
				{
					colormapu32 = (lighttable_u32_t *)colormap;
					while ((count -= 2) >= 0) // texture height is a power of 2
					{
						val = source[(frac>>FRACBITS) & heightmask];
						if (val != TRANSPARENTPIXEL)
							WriteTranslucentColumn32(colormapu32[val]);
						dest += vid.width;
						frac += fracstep;
						val = source[(frac>>FRACBITS) & heightmask];
						if (val != TRANSPARENTPIXEL)
							WriteTranslucentColumn32(colormapu32[val]);
						dest += vid.width;
						frac += fracstep;
					}
					if (count & 1)
					{
						val = source[(frac>>FRACBITS) & heightmask];
						if (val != TRANSPARENTPIXEL)
							WriteTranslucentColumn32(colormapu32[val]);
					}
				}
			}
			else if (dc_picfmt == PICFMT_PATCH32)
			{
				while ((count -= 2) >= 0) // texture height is a power of 2
				{
					valu32 = sourceu32[(frac>>FRACBITS) & heightmask];
					if (R_GetRgbaA(valu32))
						*dest = TC_TranslucentColorMix(valu32, *dest, dc_alpha);
					dest += vid.width;
					frac += fracstep;
					valu32 = sourceu32[(frac>>FRACBITS) & heightmask];
					if (R_GetRgbaA(valu32))
						*dest = TC_TranslucentColorMix(valu32, *dest, dc_alpha);
					dest += vid.width;
					frac += fracstep;
				}
				if (count & 1)
				{
					valu32 = sourceu32[(frac>>FRACBITS) & heightmask];
					if (R_GetRgbaA(valu32))
						*dest = TC_TranslucentColorMix(valu32, *dest, dc_alpha);
				}
			}
		}
	}
}

/**	\brief The R_DrawShadeColumn_32 function
	Experiment to make software go faster. Taken from the Boom source
*/
void R_DrawShadeColumn_32(void)
{
#if 0
	register INT32 count;
	register UINT32 *dest;
	register fixed_t frac, fracstep;

	// check out coords for src*
	if ((dc_yl < 0) || (dc_x >= vid.width))
		return;

	count = dc_yh - dc_yl;
	if (count < 0)
		return;

#ifdef RANGECHECK
	if ((unsigned)dc_x >= (unsigned)vid.width || dc_yl < 0 || dc_yh >= vid.height)
		I_Error("R_DrawShadeColumn_32: %d to %d at %d", dc_yl, dc_yh, dc_x);
#endif

	// FIXME. As above.
	//dest = ylookup[dc_yl] + columnofs[dc_x];
	dest = &topleft_u32[dc_yl*vid.width + dc_x];

	// Looks familiar.
	fracstep = dc_iscale;
	//frac = dc_texturemid + (dc_yl - centery)*fracstep;
	frac = (dc_texturemid + FixedMul((dc_yl << FRACBITS) - centeryfrac, fracstep))*(!dc_hires);

	// Here we do an additional index re-mapping.
	do
	{
		*dest = colormaps[(dc_source[frac>>FRACBITS] <<8) + (*dest)];
		dest += vid.width;
		frac += fracstep;
	} while (count--);
#endif
}

/**	\brief The R_DrawTranslucentColumn_32 function
	I've made an asm routine for the transparency, because it slows down
	a lot in 640x480 with big sprites (bfg on all screen, or transparent
	walls on fullscreen)
*/
void R_DrawTranslucentColumn_32(void)
{
	register INT32 count;
	register UINT32 *dest;
	register fixed_t frac, fracstep;

	count = dc_yh - dc_yl + 1;

	if (count <= 0) // Zero length, column does not exceed a pixel.
		return;

#ifdef RANGECHECK
	if ((unsigned)dc_x >= (unsigned)vid.width || dc_yl < 0 || dc_yh >= vid.height)
		I_Error("R_DrawTranslucentColumn_32: %d to %d at %d", dc_yl, dc_yh, dc_x);
#endif

	// FIXME. As above.
	//dest = ylookup[dc_yl] + columnofs[dc_x];
	dest = &topleft_u32[dc_yl*vid.width + dc_x];

	// Looks familiar.
	fracstep = dc_iscale;
	//frac = dc_texturemid + (dc_yl - centery)*fracstep;
	frac = (dc_texturemid + FixedMul((dc_yl << FRACBITS) - centeryfrac, fracstep))*(!dc_hires);

	// Inner loop that does the actual texture mapping, e.g. a DDA-like scaling.
	// This is as fast as it gets.
	{
		register const UINT8 *source = dc_source;
		register const UINT32 *sourceu32 = (UINT32 *)dc_source;
		register lighttable_t *colormap = dc_colormap;
		register lighttable_u32_t *colormapu32 = NULL;
		register INT32 heightmask = dc_texheight - 1;
		if (dc_texheight & heightmask)
		{
			heightmask++;
			heightmask <<= FRACBITS;

			if (frac < 0)
				while ((frac += heightmask) < 0)
					;
			else
				while (frac >= heightmask)
					frac -= heightmask;

			if (dc_picfmt == PICFMT_PATCH)
			{
				if (dc_colmapstyle == TC_COLORMAPSTYLE_8BPP)
				{
					do
					{
						// Re-map color indices from wall texture column
						// using a lighting/special effects LUT.
						// heightmask is the Tutti-Frutti fix
						WriteTranslucentColumn(colormap[source[frac>>FRACBITS]]);
						dest += vid.width;
						if ((frac += fracstep) >= heightmask)
							frac -= heightmask;
					}
					while (--count);
				}
				else if (dc_colmapstyle == TC_COLORMAPSTYLE_32BPP)
				{
					colormapu32 = (lighttable_u32_t *)colormap;
					do
					{
						// Re-map color indices from wall texture column
						// using a lighting/special effects LUT.
						// heightmask is the Tutti-Frutti fix
						WriteTranslucentColumn32(colormapu32[source[frac>>FRACBITS]]);
						dest += vid.width;
						if ((frac += fracstep) >= heightmask)
							frac -= heightmask;
					}
					while (--count);
				}
			}
			else if (dc_picfmt == PICFMT_PATCH32)
			{
				do
				{
					// Re-map color indices from wall texture column
					// using a lighting/special effects LUT.
					// heightmask is the Tutti-Frutti fix
					*dest = TC_TranslucentColorMix(sourceu32[frac>>FRACBITS], *dest, dc_alpha);
					dest += vid.width;
					if ((frac += fracstep) >= heightmask)
						frac -= heightmask;
				}
				while (--count);
			}
		}
		else
		{
			if (dc_picfmt == PICFMT_PATCH)
			{
				if (dc_colmapstyle == TC_COLORMAPSTYLE_8BPP)
				{
					while ((count -= 2) >= 0) // texture height is a power of 2
					{
						WriteTranslucentColumn(colormap[source[(frac>>FRACBITS)&heightmask]]);
						dest += vid.width;
						frac += fracstep;
						WriteTranslucentColumn(colormap[source[(frac>>FRACBITS)&heightmask]]);
						dest += vid.width;
						frac += fracstep;
					}
					if (count & 1)
						WriteTranslucentColumn(colormap[source[(frac>>FRACBITS)&heightmask]]);
				}
				else if (dc_colmapstyle == TC_COLORMAPSTYLE_32BPP)
				{
					colormapu32 = (lighttable_u32_t *)colormap;
					while ((count -= 2) >= 0) // texture height is a power of 2
					{
						WriteTranslucentColumn32(colormapu32[source[(frac>>FRACBITS)&heightmask]]);
						dest += vid.width;
						frac += fracstep;
						WriteTranslucentColumn32(colormapu32[source[(frac>>FRACBITS)&heightmask]]);
						dest += vid.width;
						frac += fracstep;
					}
					if (count & 1)
						WriteTranslucentColumn32(colormapu32[source[(frac>>FRACBITS)&heightmask]]);
				}
			}
			else if (dc_picfmt == PICFMT_PATCH32)
			{
				while ((count -= 2) >= 0) // texture height is a power of 2
				{
					*dest = TC_TranslucentColorMix(sourceu32[(frac>>FRACBITS)&heightmask], *dest, dc_alpha);
					dest += vid.width;
					frac += fracstep;
					*dest = TC_TranslucentColorMix(sourceu32[(frac>>FRACBITS)&heightmask], *dest, dc_alpha);
					dest += vid.width;
					frac += fracstep;
				}
				if (count & 1)
					*dest = TC_TranslucentColorMix(sourceu32[(frac>>FRACBITS)&heightmask], *dest, dc_alpha);
			}
		}
	}
}

/**	\brief The R_DrawTranslatedTranslucentColumn_32 function
	Spiffy function. Not only does it colormap a sprite, but does translucency as well.
	Uber-kudos to Cyan Helkaraxe
*/
void R_DrawTranslatedTranslucentColumn_32(void)
{
	register INT32 count;
	register UINT32 *dest;
	register fixed_t frac, fracstep;

	count = dc_yh - dc_yl + 1;

	if (count <= 0) // Zero length, column does not exceed a pixel.
		return;

	// FIXME. As above.
	//dest = ylookup[dc_yl] + columnofs[dc_x];
	dest = &topleft_u32[dc_yl*vid.width + dc_x];

	// Looks familiar.
	fracstep = dc_iscale;
	//frac = dc_texturemid + (dc_yl - centery)*fracstep;
	frac = (dc_texturemid + FixedMul((dc_yl << FRACBITS) - centeryfrac, fracstep))*(!dc_hires);

	// Inner loop that does the actual texture mapping, e.g. a DDA-like scaling.
	// This is as fast as it gets.
	{
		register const UINT8 *source = dc_source;
		register const UINT32 *sourceu32 = (UINT32 *)dc_source;
		register lighttable_t *colormap = dc_colormap;
		register lighttable_u32_t *colormapu32 = NULL;
		register INT32 heightmask = dc_texheight - 1;
		if (dc_texheight & heightmask)
		{
			heightmask++;
			heightmask <<= FRACBITS;

			if (frac < 0)
				while ((frac += heightmask) < 0)
					;
			else
				while (frac >= heightmask)
					frac -= heightmask;

			if (dc_picfmt == PICFMT_PATCH)
			{
				if (dc_colmapstyle == TC_COLORMAPSTYLE_8BPP)
				{
					do
					{
						// Re-map color indices from wall texture column
						//  using a lighting/special effects LUT.
						// heightmask is the Tutti-Frutti fix
						WriteTranslucentColumn(colormap[dc_translation[source[frac>>FRACBITS]]]);
						dest += vid.width;
						if ((frac += fracstep) >= heightmask)
							frac -= heightmask;
					}
					while (--count);
				}
				else if (dc_colmapstyle == TC_COLORMAPSTYLE_32BPP)
				{
					colormapu32 = (lighttable_u32_t *)colormap;
					do
					{
						// Re-map color indices from wall texture column
						//  using a lighting/special effects LUT.
						// heightmask is the Tutti-Frutti fix
						WriteTranslucentColumn32(colormapu32[dc_translation[source[frac>>FRACBITS]]]);
						dest += vid.width;
						if ((frac += fracstep) >= heightmask)
							frac -= heightmask;
					}
					while (--count);
				}
			}
			else if (dc_picfmt == PICFMT_PATCH32)
			{
				do
				{
					// Re-map color indices from wall texture column
					//  using a lighting/special effects LUT.
					// heightmask is the Tutti-Frutti fix
					*dest = TC_TranslucentColorMix(sourceu32[frac>>FRACBITS], *dest, dc_alpha);
					dest += vid.width;
					if ((frac += fracstep) >= heightmask)
						frac -= heightmask;
				}
				while (--count);
			}
		}
		else
		{
			if (dc_picfmt == PICFMT_PATCH)
			{
				if (dc_colmapstyle == TC_COLORMAPSTYLE_8BPP)
				{
					while ((count -= 2) >= 0) // texture height is a power of 2
					{
						WriteTranslucentColumn(colormap[dc_translation[source[(frac>>FRACBITS)&heightmask]]]);
						dest += vid.width;
						frac += fracstep;
						WriteTranslucentColumn(colormap[dc_translation[source[(frac>>FRACBITS)&heightmask]]]);
						dest += vid.width;
						frac += fracstep;
					}
					if (count & 1)
						WriteTranslucentColumn(colormap[dc_translation[source[(frac>>FRACBITS)&heightmask]]]);
				}
				else if (dc_colmapstyle == TC_COLORMAPSTYLE_32BPP)
				{
					colormapu32 = (lighttable_u32_t *)colormap;
					while ((count -= 2) >= 0) // texture height is a power of 2
					{
						WriteTranslucentColumn32(colormapu32[dc_translation[source[(frac>>FRACBITS)&heightmask]]]);
						dest += vid.width;
						frac += fracstep;
						WriteTranslucentColumn32(colormapu32[dc_translation[source[(frac>>FRACBITS)&heightmask]]]);
						dest += vid.width;
						frac += fracstep;
					}
					if (count & 1)
						WriteTranslucentColumn32(colormapu32[dc_translation[source[(frac>>FRACBITS)&heightmask]]]);
				}
			}
			else if (dc_picfmt == PICFMT_PATCH32)
			{
				while ((count -= 2) >= 0) // texture height is a power of 2
				{
					*dest = TC_TranslucentColorMix(sourceu32[(frac>>FRACBITS)&heightmask], *dest, dc_alpha);
					dest += vid.width;
					frac += fracstep;
					*dest = TC_TranslucentColorMix(sourceu32[(frac>>FRACBITS)&heightmask], *dest, dc_alpha);
					dest += vid.width;
					frac += fracstep;
				}
				if (count & 1)
					*dest = TC_TranslucentColorMix(sourceu32[(frac>>FRACBITS)&heightmask], *dest, dc_alpha);
			}
		}
	}
}

/**	\brief The R_DrawTranslatedColumn_32 function
	Draw columns up to 128 high but remap the green ramp to other colors

  \warning STILL NOT IN ASM, TO DO..
*/
void R_DrawTranslatedColumn_32(void)
{
	register INT32 count;
	register UINT32 *dest;
	register fixed_t frac, fracstep;
	register const UINT8 *source = dc_source;
	register const UINT32 *sourceu32 = (UINT32 *)dc_source;
	register lighttable_t *colormap = dc_colormap;
	register lighttable_u32_t *colormapu32 = NULL;

	count = dc_yh - dc_yl;
	if (count < 0)
		return;

#ifdef RANGECHECK
	if ((unsigned)dc_x >= (unsigned)vid.width || dc_yl < 0 || dc_yh >= vid.height)
		I_Error("R_DrawTranslatedColumn_32: %d to %d at %d", dc_yl, dc_yh, dc_x);
#endif

	// FIXME. As above.
	//dest = ylookup[dc_yl] + columnofs[dc_x];
	dest = &topleft_u32[dc_yl*vid.width + dc_x];

	// Looks familiar.
	fracstep = dc_iscale;
	//frac = dc_texturemid + (dc_yl-centery)*fracstep;
	frac = (dc_texturemid + FixedMul((dc_yl << FRACBITS) - centeryfrac, fracstep))*(!dc_hires);

	// Here we do an additional index re-mapping.
	if (dc_picfmt == PICFMT_PATCH)
	{
		if (dc_colmapstyle == TC_COLORMAPSTYLE_8BPP)
		{
			do
			{
				// Translation tables are used
				//  to map certain colorramps to other ones,
				//  used with PLAY sprites.
				// Thus the "green" ramp of the player 0 sprite
				//  is mapped to gray, red, black/indigo.
				*dest = GetTrueColor(colormap[dc_translation[source[frac>>FRACBITS]]]);
				dest += vid.width;
				frac += fracstep;
			} while (count--);
		}
		else if (dc_colmapstyle == TC_COLORMAPSTYLE_32BPP)
		{
			colormapu32 = (lighttable_u32_t *)colormap;
			do
			{
				// Translation tables are used
				//  to map certain colorramps to other ones,
				//  used with PLAY sprites.
				// Thus the "green" ramp of the player 0 sprite
				//  is mapped to gray, red, black/indigo.
				*dest = (colormapu32[dc_translation[source[frac>>FRACBITS]]]);
				dest += vid.width;
				frac += fracstep;
			} while (count--);
		}
	}
	else if (dc_picfmt == PICFMT_PATCH32)
	{
		do
		{
			// Translation tables are used
			//  to map certain colorramps to other ones,
			//  used with PLAY sprites.
			// Thus the "green" ramp of the player 0 sprite
			//  is mapped to gray, red, black/indigo.
			*dest = TC_ColorMix(sourceu32[frac>>FRACBITS], *dest);
			dest += vid.width;
			frac += fracstep;
		} while (count--);
	}
}

// ==========================================================================
// SPANS
// ==========================================================================

/**	\brief The R_DrawSpan_32 function
	Draws the actual span.
*/
void R_DrawSpan_32 (void)
{
	fixed_t xposition;
	fixed_t yposition;
	fixed_t xstep, ystep;

	UINT8 *source;
	UINT32 *sourceu32;

	UINT8 *colormap = NULL;
	UINT32 *colormapu32 = NULL;

	UINT32 *dest;
	const UINT32 *deststop = (UINT32 *)screens[0] + vid.width * vid.height;

	size_t count = (ds_x2 - ds_x1 + 1);

	xposition = ds_xfrac; yposition = ds_yfrac;
	xstep = ds_xstep; ystep = ds_ystep;

	// SoM: we only need 6 bits for the integer part (0 thru 63) so the rest
	// can be used for the fraction part. This allows calculation of the memory address in the
	// texture with two shifts, an OR and one AND. (see below)
	// for texture sizes > 64 the amount of precision we can allow will decrease, but only by one
	// bit per power of two (obviously)
	// Ok, because I was able to eliminate the variable spot below, this function is now FASTER
	// than the original span renderer. Whodathunkit?
	xposition <<= nflatshiftup; yposition <<= nflatshiftup;
	xstep <<= nflatshiftup; ystep <<= nflatshiftup;

	source = ds_source;
	sourceu32 = (UINT32 *)ds_source;
	dest = (UINT32 *)(ylookup[ds_y] + columnofs[ds_x1]);

	if (ds_colmapstyle == TC_COLORMAPSTYLE_8BPP)
		colormap = ds_colormap;
	else if (ds_colmapstyle == TC_COLORMAPSTYLE_32BPP)
		colormapu32 = (UINT32 *)ds_colormap;

	if (dest+8 > deststop)
		return;

	if (ds_picfmt == PICFMT_FLAT)
	{
		if (ds_colmapstyle == TC_COLORMAPSTYLE_8BPP)
		{
			while (count >= 8)
			{
				// SoM: Why didn't I see this earlier? the spot variable is a waste now because we don't
				// have the uber complicated math to calculate it now, so that was a memory write we didn't
				// need!
				dest[0] = GetTrueColor(colormap[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]]);
				xposition += xstep;
				yposition += ystep;

				dest[1] = GetTrueColor(colormap[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]]);
				xposition += xstep;
				yposition += ystep;

				dest[2] = GetTrueColor(colormap[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]]);
				xposition += xstep;
				yposition += ystep;

				dest[3] = GetTrueColor(colormap[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]]);
				xposition += xstep;
				yposition += ystep;

				dest[4] = GetTrueColor(colormap[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]]);
				xposition += xstep;
				yposition += ystep;

				dest[5] = GetTrueColor(colormap[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]]);
				xposition += xstep;
				yposition += ystep;

				dest[6] = GetTrueColor(colormap[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]]);
				xposition += xstep;
				yposition += ystep;

				dest[7] = GetTrueColor(colormap[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]]);
				xposition += xstep;
				yposition += ystep;

				dest += 8;
				count -= 8;
			}
			while (count-- && dest <= deststop)
			{
				*dest = GetTrueColor(colormap[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]]);
				dest++;
				xposition += xstep;
				yposition += ystep;
			}
		}
		else if (ds_colmapstyle == TC_COLORMAPSTYLE_32BPP)
		{
			while (count >= 8)
			{
				// SoM: Why didn't I see this earlier? the spot variable is a waste now because we don't
				// have the uber complicated math to calculate it now, so that was a memory write we didn't
				// need!
				dest[0] = (colormapu32[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]]);
				xposition += xstep;
				yposition += ystep;

				dest[1] = (colormapu32[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]]);
				xposition += xstep;
				yposition += ystep;

				dest[2] = (colormapu32[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]]);
				xposition += xstep;
				yposition += ystep;

				dest[3] = (colormapu32[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]]);
				xposition += xstep;
				yposition += ystep;

				dest[4] = (colormapu32[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]]);
				xposition += xstep;
				yposition += ystep;

				dest[5] = (colormapu32[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]]);
				xposition += xstep;
				yposition += ystep;

				dest[6] = (colormapu32[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]]);
				xposition += xstep;
				yposition += ystep;

				dest[7] = (colormapu32[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]]);
				xposition += xstep;
				yposition += ystep;

				dest += 8;
				count -= 8;
			}
			while (count-- && dest <= deststop)
			{
				*dest = (colormapu32[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]]);
				dest++;
				xposition += xstep;
				yposition += ystep;
			}
		}
	}
	else if (ds_picfmt == PICFMT_FLAT32)
	{
		while (count >= 8)
		{
			// SoM: Why didn't I see this earlier? the spot variable is a waste now because we don't
			// have the uber complicated math to calculate it now, so that was a memory write we didn't
			// need!
			dest[0] = TC_ColorMix(sourceu32[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)], dest[0]);
			xposition += xstep;
			yposition += ystep;

			dest[1] = TC_ColorMix(sourceu32[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)], dest[1]);
			xposition += xstep;
			yposition += ystep;

			dest[2] = TC_ColorMix(sourceu32[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)], dest[2]);
			xposition += xstep;
			yposition += ystep;

			dest[3] = TC_ColorMix(sourceu32[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)], dest[3]);
			xposition += xstep;
			yposition += ystep;

			dest[4] = TC_ColorMix(sourceu32[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)], dest[4]);
			xposition += xstep;
			yposition += ystep;

			dest[5] = TC_ColorMix(sourceu32[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)], dest[5]);
			xposition += xstep;
			yposition += ystep;

			dest[6] = TC_ColorMix(sourceu32[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)], dest[6]);
			xposition += xstep;
			yposition += ystep;

			dest[7] = TC_ColorMix(sourceu32[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)], dest[7]);
			xposition += xstep;
			yposition += ystep;

			dest += 8;
			count -= 8;
		}
		while (count-- && dest <= deststop)
		{
			*dest = TC_ColorMix(sourceu32[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)], *dest);
			dest++;
			xposition += xstep;
			yposition += ystep;
		}
	}
}

/**	\brief The R_DrawTiltedSpan_32 function
	Draw slopes! Holy sheit!
*/
void R_DrawTiltedSpan_32(void)
{
	// x1, x2 = ds_x1, ds_x2
	int width = ds_x2 - ds_x1;
	double iz, uz, vz;
	UINT32 u, v;
	int i;

	UINT8 *source;
	UINT32 *sourceu32;

	UINT8 *colormap = NULL;
	UINT32 *colormapu32 = NULL;

	UINT32 *dest;

	double startz, startu, startv;
	double izstep, uzstep, vzstep;
	double endz, endu, endv;
	UINT32 stepu, stepv;

	iz = ds_szp->z + ds_szp->y*(centery-ds_y) + ds_szp->x*(ds_x1-centerx);

	// Lighting is simple. It's just linear interpolation from start to end
	{
		float planelightfloat = BASEVIDWIDTH*BASEVIDWIDTH/vid.width / (zeroheight - FIXED_TO_FLOAT(viewz)) / 21.0f;
		float lightstart, lightend;

		lightend = (iz + ds_szp->x*width) * planelightfloat;
		lightstart = iz * planelightfloat;

		R_CalcTiltedLighting(FLOAT_TO_FIXED(lightstart), FLOAT_TO_FIXED(lightend));
		//CONS_Printf("tilted lighting %f to %f (foc %f)\n", lightstart, lightend, focallengthf);
	}

	uz = ds_sup->z + ds_sup->y*(centery-ds_y) + ds_sup->x*(ds_x1-centerx);
	vz = ds_svp->z + ds_svp->y*(centery-ds_y) + ds_svp->x*(ds_x1-centerx);
	dest = (UINT32 *)(ylookup[ds_y] + columnofs[ds_x1]);

	source = ds_source;
	sourceu32 = (UINT32 *)ds_source;
	//colormap = ds_colormap;

#if 0	// The "perfect" reference version of this routine. Pretty slow.
		// Use it only to see how things are supposed to look.
	i = 0;
	do
	{
		double z = 1.f/iz;
		u = (INT64)(uz*z) + viewx;
		v = (INT64)(vz*z) + viewy;

		colormap = planezlight[tiltlighting[ds_x1++]] + (ds_colormap - colormaps);

		*dest = colormap[source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)]];
		dest++;
		iz += ds_szp->x;
		uz += ds_sup->x;
		vz += ds_svp->x;
	} while (--width >= 0);
#else
#define SPANSIZE 16
#define INVSPAN	0.0625f

	startz = 1.f/iz;
	startu = uz*startz;
	startv = vz*startz;

	izstep = ds_szp->x * SPANSIZE;
	uzstep = ds_sup->x * SPANSIZE;
	vzstep = ds_svp->x * SPANSIZE;
	//x1 = 0;
	width++;

	if (ds_picfmt == PICFMT_FLAT)
	{
		if (ds_colmapstyle == TC_COLORMAPSTYLE_8BPP)
		{
			while (width >= SPANSIZE)
			{
				iz += izstep;
				uz += uzstep;
				vz += vzstep;

				endz = 1.f/iz;
				endu = uz*endz;
				endv = vz*endz;
				stepu = (INT64)((endu - startu) * INVSPAN);
				stepv = (INT64)((endv - startv) * INVSPAN);
				u = (INT64)(startu) + viewx;
				v = (INT64)(startv) + viewy;

				for (i = SPANSIZE-1; i >= 0; i--)
				{
					colormap = planezlight[tiltlighting[ds_x1++]] + (ds_colormap - colormaps);
					*dest = GetTrueColor(colormap[source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)]]);
					dest++;
					u += stepu;
					v += stepv;
				}
				startu = endu;
				startv = endv;
				width -= SPANSIZE;
			}
			if (width > 0)
			{
				if (width == 1)
				{
					u = (INT64)(startu);
					v = (INT64)(startv);
					colormap = planezlight[tiltlighting[ds_x1++]] + (ds_colormap - colormaps);
					*dest = GetTrueColor(colormap[source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)]]);
				}
				else
				{
					double left = width;
					iz += ds_szp->x * left;
					uz += ds_sup->x * left;
					vz += ds_svp->x * left;

					endz = 1.f/iz;
					endu = uz*endz;
					endv = vz*endz;
					left = 1.f/left;
					stepu = (INT64)((endu - startu) * left);
					stepv = (INT64)((endv - startv) * left);
					u = (INT64)(startu) + viewx;
					v = (INT64)(startv) + viewy;

					for (; width != 0; width--)
					{
						colormap = planezlight[tiltlighting[ds_x1++]] + (ds_colormap - colormaps);
						*dest = GetTrueColor(colormap[source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)]]);
						dest++;
						u += stepu;
						v += stepv;
					}
				}
			}
		}
		else if (ds_colmapstyle == TC_COLORMAPSTYLE_32BPP)
		{
			while (width >= SPANSIZE)
			{
				iz += izstep;
				uz += uzstep;
				vz += vzstep;

				endz = 1.f/iz;
				endu = uz*endz;
				endv = vz*endz;
				stepu = (INT64)((endu - startu) * INVSPAN);
				stepv = (INT64)((endv - startv) * INVSPAN);
				u = (INT64)(startu) + viewx;
				v = (INT64)(startv) + viewy;

				for (i = SPANSIZE-1; i >= 0; i--)
				{
					colormapu32 = planezlight_u32[tiltlighting[ds_x1++]] + ((UINT32*)ds_colormap - colormaps_u32);
					*dest = (colormapu32[source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)]]);
					dest++;
					u += stepu;
					v += stepv;
				}
				startu = endu;
				startv = endv;
				width -= SPANSIZE;
			}
			if (width > 0)
			{
				if (width == 1)
				{
					u = (INT64)(startu);
					v = (INT64)(startv);
					colormapu32 = planezlight_u32[tiltlighting[ds_x1++]] + ((UINT32*)ds_colormap - colormaps_u32);
					*dest = (colormapu32[source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)]]);
				}
				else
				{
					double left = width;
					iz += ds_szp->x * left;
					uz += ds_sup->x * left;
					vz += ds_svp->x * left;

					endz = 1.f/iz;
					endu = uz*endz;
					endv = vz*endz;
					left = 1.f/left;
					stepu = (INT64)((endu - startu) * left);
					stepv = (INT64)((endv - startv) * left);
					u = (INT64)(startu) + viewx;
					v = (INT64)(startv) + viewy;

					for (; width != 0; width--)
					{
						colormapu32 = planezlight_u32[tiltlighting[ds_x1++]] + ((UINT32*)ds_colormap - colormaps_u32);
						*dest = (colormapu32[source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)]]);
						dest++;
						u += stepu;
						v += stepv;
					}
				}
			}
		}
	}
	else if (ds_picfmt == PICFMT_FLAT32)
	{
		while (width >= SPANSIZE)
		{
			iz += izstep;
			uz += uzstep;
			vz += vzstep;

			endz = 1.f/iz;
			endu = uz*endz;
			endv = vz*endz;
			stepu = (INT64)((endu - startu) * INVSPAN);
			stepv = (INT64)((endv - startv) * INVSPAN);
			u = (INT64)(startu) + viewx;
			v = (INT64)(startv) + viewy;

			for (i = SPANSIZE-1; i >= 0; i--)
			{
				dp_lighting = TC_CalcScaleLight(planezlight_u32[tiltlighting[ds_x1++]]);
				*dest = TC_ColorMix(sourceu32[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)], *dest);
				dest++;
				u += stepu;
				v += stepv;
			}
			startu = endu;
			startv = endv;
			width -= SPANSIZE;
		}
		if (width > 0)
		{
			if (width == 1)
			{
				u = (INT64)(startu);
				v = (INT64)(startv);
				dp_lighting = TC_CalcScaleLight(planezlight_u32[tiltlighting[ds_x1++]]);
				*dest = TC_ColorMix(sourceu32[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)], *dest);
			}
			else
			{
				double left = width;
				iz += ds_szp->x * left;
				uz += ds_sup->x * left;
				vz += ds_svp->x * left;

				endz = 1.f/iz;
				endu = uz*endz;
				endv = vz*endz;
				left = 1.f/left;
				stepu = (INT64)((endu - startu) * left);
				stepv = (INT64)((endv - startv) * left);
				u = (INT64)(startu) + viewx;
				v = (INT64)(startv) + viewy;

				for (; width != 0; width--)
				{
					dp_lighting = TC_CalcScaleLight(planezlight_u32[tiltlighting[ds_x1++]]);
					*dest = TC_ColorMix(sourceu32[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)], *dest);
					dest++;
					u += stepu;
					v += stepv;
				}
			}
		}
	}
#endif
}

/**	\brief The R_DrawTiltedTranslucentSpan_32 function
	Like DrawTiltedSpan, but translucent
*/
void R_DrawTiltedTranslucentSpan_32(void)
{
	// x1, x2 = ds_x1, ds_x2
	int width = ds_x2 - ds_x1;
	double iz, uz, vz;
	UINT32 u, v;
	int i;

	UINT8 *source;
	UINT32 *sourceu32;

	UINT8 *colormap = NULL;
	UINT32 *colormapu32 = NULL;

	UINT32 *dest;

	double startz, startu, startv;
	double izstep, uzstep, vzstep;
	double endz, endu, endv;
	UINT32 stepu, stepv;

	iz = ds_szp->z + ds_szp->y*(centery-ds_y) + ds_szp->x*(ds_x1-centerx);

	// Lighting is simple. It's just linear interpolation from start to end
	{
		float planelightfloat = BASEVIDWIDTH*BASEVIDWIDTH/vid.width / (zeroheight - FIXED_TO_FLOAT(viewz)) / 21.0f;
		float lightstart, lightend;

		lightend = (iz + ds_szp->x*width) * planelightfloat;
		lightstart = iz * planelightfloat;

		R_CalcTiltedLighting(FLOAT_TO_FIXED(lightstart), FLOAT_TO_FIXED(lightend));
		//CONS_Printf("tilted lighting %f to %f (foc %f)\n", lightstart, lightend, focallengthf);
	}

	uz = ds_sup->z + ds_sup->y*(centery-ds_y) + ds_sup->x*(ds_x1-centerx);
	vz = ds_svp->z + ds_svp->y*(centery-ds_y) + ds_svp->x*(ds_x1-centerx);
	dest = (UINT32 *)(ylookup[ds_y] + columnofs[ds_x1]);

	source = ds_source;
	sourceu32 = (UINT32 *)ds_source;
	//colormap = ds_colormap;

#if 0	// The "perfect" reference version of this routine. Pretty slow.
		// Use it only to see how things are supposed to look.
	i = 0;
	do
	{
		double z = 1.f/iz;
		u = (INT64)(uz*z) + viewx;
		v = (INT64)(vz*z) + viewy;

		colormap = planezlight[tiltlighting[ds_x1++]] + (ds_colormap - colormaps);
		*dest = *(ds_transmap + (colormap[source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)]] << 8) + *dest);
		dest++;
		iz += ds_szp->x;
		uz += ds_sup->x;
		vz += ds_svp->x;
	} while (--width >= 0);
#else
#define SPANSIZE 16
#define INVSPAN	0.0625f

	startz = 1.f/iz;
	startu = uz*startz;
	startv = vz*startz;

	izstep = ds_szp->x * SPANSIZE;
	uzstep = ds_sup->x * SPANSIZE;
	vzstep = ds_svp->x * SPANSIZE;
	//x1 = 0;
	width++;

	if (ds_picfmt == PICFMT_FLAT)
	{
		if (ds_colmapstyle == TC_COLORMAPSTYLE_8BPP)
		{
			while (width >= SPANSIZE)
			{
				iz += izstep;
				uz += uzstep;
				vz += vzstep;

				endz = 1.f/iz;
				endu = uz*endz;
				endv = vz*endz;
				stepu = (INT64)((endu - startu) * INVSPAN);
				stepv = (INT64)((endv - startv) * INVSPAN);
				u = (INT64)(startu) + viewx;
				v = (INT64)(startv) + viewy;

				for (i = SPANSIZE-1; i >= 0; i--)
				{
					colormap = planezlight[tiltlighting[ds_x1++]] + (ds_colormap - colormaps);
					WriteTranslucentSpan(colormap[source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)]]);
					dest++;
					u += stepu;
					v += stepv;
				}
				startu = endu;
				startv = endv;
				width -= SPANSIZE;
			}
			if (width > 0)
			{
				if (width == 1)
				{
					u = (INT64)(startu);
					v = (INT64)(startv);
					colormap = planezlight[tiltlighting[ds_x1++]] + (ds_colormap - colormaps);
					WriteTranslucentSpan(colormap[source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)]]);
				}
				else
				{
					double left = width;
					iz += ds_szp->x * left;
					uz += ds_sup->x * left;
					vz += ds_svp->x * left;

					endz = 1.f/iz;
					endu = uz*endz;
					endv = vz*endz;
					left = 1.f/left;
					stepu = (INT64)((endu - startu) * left);
					stepv = (INT64)((endv - startv) * left);
					u = (INT64)(startu) + viewx;
					v = (INT64)(startv) + viewy;

					for (; width != 0; width--)
					{
						colormap = planezlight[tiltlighting[ds_x1++]] + (ds_colormap - colormaps);
						WriteTranslucentSpan(colormap[source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)]]);
						dest++;
						u += stepu;
						v += stepv;
					}
				}
			}
		}
		else if (ds_colmapstyle == TC_COLORMAPSTYLE_32BPP)
		{
			while (width >= SPANSIZE)
			{
				iz += izstep;
				uz += uzstep;
				vz += vzstep;

				endz = 1.f/iz;
				endu = uz*endz;
				endv = vz*endz;
				stepu = (INT64)((endu - startu) * INVSPAN);
				stepv = (INT64)((endv - startv) * INVSPAN);
				u = (INT64)(startu) + viewx;
				v = (INT64)(startv) + viewy;

				for (i = SPANSIZE-1; i >= 0; i--)
				{
					colormapu32 = planezlight_u32[tiltlighting[ds_x1++]] + ((UINT32*)ds_colormap - colormaps_u32);
					WriteTranslucentSpan32(colormapu32[source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)]]);
					dest++;
					u += stepu;
					v += stepv;
				}
				startu = endu;
				startv = endv;
				width -= SPANSIZE;
			}
			if (width > 0)
			{
				if (width == 1)
				{
					u = (INT64)(startu);
					v = (INT64)(startv);
					colormapu32 = planezlight_u32[tiltlighting[ds_x1++]] + ((UINT32*)ds_colormap - colormaps_u32);
					WriteTranslucentSpan32(colormapu32[source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)]]);
				}
				else
				{
					double left = width;
					iz += ds_szp->x * left;
					uz += ds_sup->x * left;
					vz += ds_svp->x * left;

					endz = 1.f/iz;
					endu = uz*endz;
					endv = vz*endz;
					left = 1.f/left;
					stepu = (INT64)((endu - startu) * left);
					stepv = (INT64)((endv - startv) * left);
					u = (INT64)(startu) + viewx;
					v = (INT64)(startv) + viewy;

					for (; width != 0; width--)
					{
						colormapu32 = planezlight_u32[tiltlighting[ds_x1++]] + ((UINT32*)ds_colormap - colormaps_u32);
						WriteTranslucentSpan32(colormapu32[source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)]]);
						dest++;
						u += stepu;
						v += stepv;
					}
				}
			}
		}
	}
	else if (ds_picfmt == PICFMT_FLAT32)
	{
		while (width >= SPANSIZE)
		{
			iz += izstep;
			uz += uzstep;
			vz += vzstep;

			endz = 1.f/iz;
			endu = uz*endz;
			endv = vz*endz;
			stepu = (INT64)((endu - startu) * INVSPAN);
			stepv = (INT64)((endv - startv) * INVSPAN);
			u = (INT64)(startu) + viewx;
			v = (INT64)(startv) + viewy;

			for (i = SPANSIZE-1; i >= 0; i--)
			{
				dp_lighting = TC_CalcScaleLight(planezlight_u32[tiltlighting[ds_x1++]]);
				*dest = TC_TranslucentColorMix(sourceu32[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)], *dest, ds_alpha);
				dest++;
				u += stepu;
				v += stepv;
			}
			startu = endu;
			startv = endv;
			width -= SPANSIZE;
		}
		if (width > 0)
		{
			if (width == 1)
			{
				u = (INT64)(startu);
				v = (INT64)(startv);
				dp_lighting = TC_CalcScaleLight(planezlight_u32[tiltlighting[ds_x1++]]);
				*dest = TC_TranslucentColorMix(sourceu32[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)], *dest, ds_alpha);
			}
			else
			{
				double left = width;
				iz += ds_szp->x * left;
				uz += ds_sup->x * left;
				vz += ds_svp->x * left;

				endz = 1.f/iz;
				endu = uz*endz;
				endv = vz*endz;
				left = 1.f/left;
				stepu = (INT64)((endu - startu) * left);
				stepv = (INT64)((endv - startv) * left);
				u = (INT64)(startu) + viewx;
				v = (INT64)(startv) + viewy;

				for (; width != 0; width--)
				{
					dp_lighting = TC_CalcScaleLight(planezlight_u32[tiltlighting[ds_x1++]]);
					*dest = TC_TranslucentColorMix(sourceu32[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)], *dest, ds_alpha);
					dest++;
					u += stepu;
					v += stepv;
				}
			}
		}
	}
#endif
}

#ifndef NOWATER
/**	\brief The R_DrawTiltedTranslucentWaterSpan_32 function
	Like DrawTiltedTranslucentSpan, but for water
*/
void R_DrawTiltedTranslucentWaterSpan_32(void)
{
	// x1, x2 = ds_x1, ds_x2
	int width = ds_x2 - ds_x1;
	double iz, uz, vz;
	UINT32 u, v;
	int i;

	UINT8 *source;
	UINT32 *sourceu32;

	UINT8 *colormap = NULL;
	UINT32 *colormapu32 = NULL;

	UINT32 *dest;
	UINT32 *dsrc;

	double startz, startu, startv;
	double izstep, uzstep, vzstep;
	double endz, endu, endv;
	UINT32 stepu, stepv;

	iz = ds_szp->z + ds_szp->y*(centery-ds_y) + ds_szp->x*(ds_x1-centerx);

	// Lighting is simple. It's just linear interpolation from start to end
	{
		float planelightfloat = BASEVIDWIDTH*BASEVIDWIDTH/vid.width / (zeroheight - FIXED_TO_FLOAT(viewz)) / 21.0f;
		float lightstart, lightend;

		lightend = (iz + ds_szp->x*width) * planelightfloat;
		lightstart = iz * planelightfloat;

		R_CalcTiltedLighting(FLOAT_TO_FIXED(lightstart), FLOAT_TO_FIXED(lightend));
		//CONS_Printf("tilted lighting %f to %f (foc %f)\n", lightstart, lightend, focallengthf);
	}

	uz = ds_sup->z + ds_sup->y*(centery-ds_y) + ds_sup->x*(ds_x1-centerx);
	vz = ds_svp->z + ds_svp->y*(centery-ds_y) + ds_svp->x*(ds_x1-centerx);
	dest = (UINT32 *)(ylookup[ds_y] + columnofs[ds_x1]);

	dsrc = ((UINT32 *)screens[1]) + (ds_y+ds_bgofs)*vid.width + ds_x1;
	source = ds_source;
	sourceu32 = (UINT32 *)ds_source;
	//colormap = ds_colormap;

#if 0	// The "perfect" reference version of this routine. Pretty slow.
		// Use it only to see how things are supposed to look.
	i = 0;
	do
	{
		double z = 1.f/iz;
		u = (INT64)(uz*z) + viewx;
		v = (INT64)(vz*z) + viewy;

		colormap = planezlight[tiltlighting[ds_x1++]] + (ds_colormap - colormaps);
		*dest = *(ds_transmap + (colormap[source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)]] << 8) + *dsrc++);
		dest++;
		iz += ds_szp->x;
		uz += ds_sup->x;
		vz += ds_svp->x;
	} while (--width >= 0);
#else
#define SPANSIZE 16
#define INVSPAN	0.0625f

	startz = 1.f/iz;
	startu = uz*startz;
	startv = vz*startz;

	izstep = ds_szp->x * SPANSIZE;
	uzstep = ds_sup->x * SPANSIZE;
	vzstep = ds_svp->x * SPANSIZE;
	//x1 = 0;
	width++;

	if (ds_picfmt == PICFMT_FLAT)
	{
		if (ds_colmapstyle == TC_COLORMAPSTYLE_8BPP)
		{
			while (width >= SPANSIZE)
			{
				iz += izstep;
				uz += uzstep;
				vz += vzstep;

				endz = 1.f/iz;
				endu = uz*endz;
				endv = vz*endz;
				stepu = (INT64)((endu - startu) * INVSPAN);
				stepv = (INT64)((endv - startv) * INVSPAN);
				u = (INT64)(startu) + viewx;
				v = (INT64)(startv) + viewy;

				for (i = SPANSIZE-1; i >= 0; i--)
				{
					colormap = planezlight[tiltlighting[ds_x1++]] + (ds_colormap - colormaps);
					WriteTranslucentWaterSpan(colormap[source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)]]);
					dest++;
					u += stepu;
					v += stepv;
				}
				startu = endu;
				startv = endv;
				width -= SPANSIZE;
			}
			if (width > 0)
			{
				if (width == 1)
				{
					u = (INT64)(startu);
					v = (INT64)(startv);
					colormap = planezlight[tiltlighting[ds_x1++]] + (ds_colormap - colormaps);
					WriteTranslucentWaterSpan(colormap[source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)]]);
				}
				else
				{
					double left = width;
					iz += ds_szp->x * left;
					uz += ds_sup->x * left;
					vz += ds_svp->x * left;

					endz = 1.f/iz;
					endu = uz*endz;
					endv = vz*endz;
					left = 1.f/left;
					stepu = (INT64)((endu - startu) * left);
					stepv = (INT64)((endv - startv) * left);
					u = (INT64)(startu) + viewx;
					v = (INT64)(startv) + viewy;

					for (; width != 0; width--)
					{
						colormap = planezlight[tiltlighting[ds_x1++]] + (ds_colormap - colormaps);
						WriteTranslucentWaterSpan(colormap[source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)]]);
						dest++;
						u += stepu;
						v += stepv;
					}
				}
			}
		}
		else if (ds_colmapstyle == TC_COLORMAPSTYLE_32BPP)
		{
			while (width >= SPANSIZE)
			{
				iz += izstep;
				uz += uzstep;
				vz += vzstep;

				endz = 1.f/iz;
				endu = uz*endz;
				endv = vz*endz;
				stepu = (INT64)((endu - startu) * INVSPAN);
				stepv = (INT64)((endv - startv) * INVSPAN);
				u = (INT64)(startu) + viewx;
				v = (INT64)(startv) + viewy;

				for (i = SPANSIZE-1; i >= 0; i--)
				{
					colormapu32 = planezlight_u32[tiltlighting[ds_x1++]] + ((UINT32*)ds_colormap - colormaps_u32);
					WriteTranslucentWaterSpan32(colormapu32[source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)]]);
					dest++;
					u += stepu;
					v += stepv;
				}
				startu = endu;
				startv = endv;
				width -= SPANSIZE;
			}
			if (width > 0)
			{
				if (width == 1)
				{
					u = (INT64)(startu);
					v = (INT64)(startv);
					colormapu32 = planezlight_u32[tiltlighting[ds_x1++]] + ((UINT32*)ds_colormap - colormaps_u32);
					WriteTranslucentWaterSpan32(colormapu32[source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)]]);
				}
				else
				{
					double left = width;
					iz += ds_szp->x * left;
					uz += ds_sup->x * left;
					vz += ds_svp->x * left;

					endz = 1.f/iz;
					endu = uz*endz;
					endv = vz*endz;
					left = 1.f/left;
					stepu = (INT64)((endu - startu) * left);
					stepv = (INT64)((endv - startv) * left);
					u = (INT64)(startu) + viewx;
					v = (INT64)(startv) + viewy;

					for (; width != 0; width--)
					{
						colormapu32 = planezlight_u32[tiltlighting[ds_x1++]] + ((UINT32*)ds_colormap - colormaps_u32);
						WriteTranslucentWaterSpan32(colormapu32[source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)]]);
						dest++;
						u += stepu;
						v += stepv;
					}
				}
			}
		}
	}
	else if (ds_picfmt == PICFMT_FLAT32)
	{
		while (width >= SPANSIZE)
		{
			iz += izstep;
			uz += uzstep;
			vz += vzstep;

			endz = 1.f/iz;
			endu = uz*endz;
			endv = vz*endz;
			stepu = (INT64)((endu - startu) * INVSPAN);
			stepv = (INT64)((endv - startv) * INVSPAN);
			u = (INT64)(startu) + viewx;
			v = (INT64)(startv) + viewy;

			for (i = SPANSIZE-1; i >= 0; i--)
			{
				dp_lighting = TC_CalcScaleLight(planezlight_u32[tiltlighting[ds_x1++]]);
				*dest = TC_TranslucentColorMix(sourceu32[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)], *dsrc++, ds_alpha);
				dest++;
				u += stepu;
				v += stepv;
			}
			startu = endu;
			startv = endv;
			width -= SPANSIZE;
		}
		if (width > 0)
		{
			if (width == 1)
			{
				u = (INT64)(startu);
				v = (INT64)(startv);
				dp_lighting = TC_CalcScaleLight(planezlight_u32[tiltlighting[ds_x1++]]);
				*dest = TC_TranslucentColorMix(sourceu32[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)], *dsrc++, ds_alpha);
			}
			else
			{
				double left = width;
				iz += ds_szp->x * left;
				uz += ds_sup->x * left;
				vz += ds_svp->x * left;

				endz = 1.f/iz;
				endu = uz*endz;
				endv = vz*endz;
				left = 1.f/left;
				stepu = (INT64)((endu - startu) * left);
				stepv = (INT64)((endv - startv) * left);
				u = (INT64)(startu) + viewx;
				v = (INT64)(startv) + viewy;

				for (; width != 0; width--)
				{
					dp_lighting = TC_CalcScaleLight(planezlight_u32[tiltlighting[ds_x1++]]);
					*dest = TC_TranslucentColorMix(sourceu32[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)], *dsrc++, ds_alpha);
					dest++;
					u += stepu;
					v += stepv;
				}
			}
		}
	}
#endif
}
#endif // NOWATER

void R_DrawTiltedSplat_32(void)
{
	// x1, x2 = ds_x1, ds_x2
	int width = ds_x2 - ds_x1;
	double iz, uz, vz;
	UINT32 u, v;
	int i;

	UINT8 *source;
	UINT32 *sourceu32;

	UINT8 *colormap = NULL;
	UINT32 *colormapu32 = NULL;

	UINT32 *dest;

	UINT8 val;
	UINT32 valu32;

	double startz, startu, startv;
	double izstep, uzstep, vzstep;
	double endz, endu, endv;
	UINT32 stepu, stepv;

	iz = ds_szp->z + ds_szp->y*(centery-ds_y) + ds_szp->x*(ds_x1-centerx);

	// Lighting is simple. It's just linear interpolation from start to end
	{
		float planelightfloat = BASEVIDWIDTH*BASEVIDWIDTH/vid.width / (zeroheight - FIXED_TO_FLOAT(viewz)) / 21.0f;
		float lightstart, lightend;

		lightend = (iz + ds_szp->x*width) * planelightfloat;
		lightstart = iz * planelightfloat;

		R_CalcTiltedLighting(FLOAT_TO_FIXED(lightstart), FLOAT_TO_FIXED(lightend));
		//CONS_Printf("tilted lighting %f to %f (foc %f)\n", lightstart, lightend, focallengthf);
	}

	uz = ds_sup->z + ds_sup->y*(centery-ds_y) + ds_sup->x*(ds_x1-centerx);
	vz = ds_svp->z + ds_svp->y*(centery-ds_y) + ds_svp->x*(ds_x1-centerx);
	dest = (UINT32 *)(ylookup[ds_y] + columnofs[ds_x1]);

	source = ds_source;
	sourceu32 = (UINT32 *)ds_source;
	//colormap = ds_colormap;

#if 0	// The "perfect" reference version of this routine. Pretty slow.
		// Use it only to see how things are supposed to look.
	i = 0;
	do
	{
		double z = 1.f/iz;
		u = (INT64)(uz*z) + viewx;
		v = (INT64)(vz*z) + viewy;

		colormap = planezlight[tiltlighting[ds_x1++]] + (ds_colormap - colormaps);

		val = source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)];
		if (val != TRANSPARENTPIXEL)
			*dest = colormap[val];

		dest++;
		iz += ds_szp->x;
		uz += ds_sup->x;
		vz += ds_svp->x;
	} while (--width >= 0);
#else
#define SPANSIZE 16
#define INVSPAN	0.0625f

	startz = 1.f/iz;
	startu = uz*startz;
	startv = vz*startz;

	izstep = ds_szp->x * SPANSIZE;
	uzstep = ds_sup->x * SPANSIZE;
	vzstep = ds_svp->x * SPANSIZE;
	//x1 = 0;
	width++;

	if (ds_picfmt == PICFMT_FLAT)
	{
		if (ds_colmapstyle == TC_COLORMAPSTYLE_8BPP)
		{
			while (width >= SPANSIZE)
			{
				iz += izstep;
				uz += uzstep;
				vz += vzstep;

				endz = 1.f/iz;
				endu = uz*endz;
				endv = vz*endz;
				stepu = (INT64)((endu - startu) * INVSPAN);
				stepv = (INT64)((endv - startv) * INVSPAN);
				u = (INT64)(startu) + viewx;
				v = (INT64)(startv) + viewy;

				for (i = SPANSIZE-1; i >= 0; i--)
				{
					colormap = planezlight[tiltlighting[ds_x1++]] + (ds_colormap - colormaps);
					val = source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)];
					if (val != TRANSPARENTPIXEL)
						*dest = GetTrueColor(colormap[val]);
					dest++;
					u += stepu;
					v += stepv;
				}
				startu = endu;
				startv = endv;
				width -= SPANSIZE;
			}
			if (width > 0)
			{
				if (width == 1)
				{
					u = (INT64)(startu);
					v = (INT64)(startv);
					colormap = planezlight[tiltlighting[ds_x1++]] + (ds_colormap - colormaps);
					val = source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)];
					if (val != TRANSPARENTPIXEL)
						*dest = GetTrueColor(colormap[val]);
				}
				else
				{
					double left = width;
					iz += ds_szp->x * left;
					uz += ds_sup->x * left;
					vz += ds_svp->x * left;

					endz = 1.f/iz;
					endu = uz*endz;
					endv = vz*endz;
					left = 1.f/left;
					stepu = (INT64)((endu - startu) * left);
					stepv = (INT64)((endv - startv) * left);
					u = (INT64)(startu) + viewx;
					v = (INT64)(startv) + viewy;

					for (; width != 0; width--)
					{
						colormap = planezlight[tiltlighting[ds_x1++]] + (ds_colormap - colormaps);
						val = source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)];
						if (val != TRANSPARENTPIXEL)
							*dest = GetTrueColor(colormap[val]);
						dest++;
						u += stepu;
						v += stepv;
					}
				}
			}
		}
		else if (ds_colmapstyle == TC_COLORMAPSTYLE_32BPP)
		{
			while (width >= SPANSIZE)
			{
				iz += izstep;
				uz += uzstep;
				vz += vzstep;

				endz = 1.f/iz;
				endu = uz*endz;
				endv = vz*endz;
				stepu = (INT64)((endu - startu) * INVSPAN);
				stepv = (INT64)((endv - startv) * INVSPAN);
				u = (INT64)(startu) + viewx;
				v = (INT64)(startv) + viewy;

				for (i = SPANSIZE-1; i >= 0; i--)
				{
					colormapu32 = planezlight_u32[tiltlighting[ds_x1++]] + ((UINT32*)ds_colormap - colormaps_u32);
					val = source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)];
					if (val != TRANSPARENTPIXEL)
						*dest = (colormapu32[val]);
					dest++;
					u += stepu;
					v += stepv;
				}
				startu = endu;
				startv = endv;
				width -= SPANSIZE;
			}
			if (width > 0)
			{
				if (width == 1)
				{
					u = (INT64)(startu);
					v = (INT64)(startv);
					colormapu32 = planezlight_u32[tiltlighting[ds_x1++]] + ((UINT32*)ds_colormap - colormaps_u32);
					val = source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)];
					if (val != TRANSPARENTPIXEL)
						*dest = (colormapu32[val]);
				}
				else
				{
					double left = width;
					iz += ds_szp->x * left;
					uz += ds_sup->x * left;
					vz += ds_svp->x * left;

					endz = 1.f/iz;
					endu = uz*endz;
					endv = vz*endz;
					left = 1.f/left;
					stepu = (INT64)((endu - startu) * left);
					stepv = (INT64)((endv - startv) * left);
					u = (INT64)(startu) + viewx;
					v = (INT64)(startv) + viewy;

					for (; width != 0; width--)
					{
						colormapu32 = planezlight_u32[tiltlighting[ds_x1++]] + ((UINT32*)ds_colormap - colormaps_u32);
						val = source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)];
						if (val != TRANSPARENTPIXEL)
							*dest = (colormapu32[val]);
						dest++;
						u += stepu;
						v += stepv;
					}
				}
			}
		}
	}
	else if (ds_picfmt == PICFMT_FLAT32)
	{
		while (width >= SPANSIZE)
		{
			iz += izstep;
			uz += uzstep;
			vz += vzstep;

			endz = 1.f/iz;
			endu = uz*endz;
			endv = vz*endz;
			stepu = (INT64)((endu - startu) * INVSPAN);
			stepv = (INT64)((endv - startv) * INVSPAN);
			u = (INT64)(startu) + viewx;
			v = (INT64)(startv) + viewy;

			for (i = SPANSIZE-1; i >= 0; i--)
			{
				dp_lighting = TC_CalcScaleLight(planezlight_u32[tiltlighting[ds_x1++]]);
				valu32 = sourceu32[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)];
				if (R_GetRgbaA(valu32))
					*dest = TC_ColorMix(valu32, *dest);
				dest++;
				u += stepu;
				v += stepv;
			}
			startu = endu;
			startv = endv;
			width -= SPANSIZE;
		}
		if (width > 0)
		{
			if (width == 1)
			{
				u = (INT64)(startu);
				v = (INT64)(startv);
				dp_lighting = TC_CalcScaleLight(planezlight_u32[tiltlighting[ds_x1++]]);
				valu32 = sourceu32[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)];
				if (R_GetRgbaA(valu32))
					*dest = TC_ColorMix(valu32, *dest);
			}
			else
			{
				double left = width;
				iz += ds_szp->x * left;
				uz += ds_sup->x * left;
				vz += ds_svp->x * left;

				endz = 1.f/iz;
				endu = uz*endz;
				endv = vz*endz;
				left = 1.f/left;
				stepu = (INT64)((endu - startu) * left);
				stepv = (INT64)((endv - startv) * left);
				u = (INT64)(startu) + viewx;
				v = (INT64)(startv) + viewy;

				for (; width != 0; width--)
				{
					dp_lighting = TC_CalcScaleLight(planezlight_u32[tiltlighting[ds_x1++]]);
					valu32 = sourceu32[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)];
					if (R_GetRgbaA(valu32))
						*dest = TC_ColorMix(valu32, *dest);
					dest++;
					u += stepu;
					v += stepv;
				}
			}
		}
	}
#endif
}

/**	\brief The R_DrawSplat_32 function
	Just like R_DrawSpan_32, but skips transparent pixels.
*/
void R_DrawSplat_32 (void)
{
	fixed_t xposition;
	fixed_t yposition;
	fixed_t xstep, ystep;

	UINT8 *source;
	UINT32 *sourceu32;

	UINT8 *colormap = NULL;
	UINT32 *colormapu32 = NULL;

	UINT32 *dest;
	const UINT32 *deststop = (UINT32 *)screens[0] + vid.width * vid.height;

	size_t count = (ds_x2 - ds_x1 + 1);
	UINT32 val;

	xposition = ds_xfrac; yposition = ds_yfrac;
	xstep = ds_xstep; ystep = ds_ystep;

	// SoM: we only need 6 bits for the integer part (0 thru 63) so the rest
	// can be used for the fraction part. This allows calculation of the memory address in the
	// texture with two shifts, an OR and one AND. (see below)
	// for texture sizes > 64 the amount of precision we can allow will decrease, but only by one
	// bit per power of two (obviously)
	// Ok, because I was able to eliminate the variable spot below, this function is now FASTER
	// than the original span renderer. Whodathunkit?
	xposition <<= nflatshiftup; yposition <<= nflatshiftup;
	xstep <<= nflatshiftup; ystep <<= nflatshiftup;

	source = ds_source;
	sourceu32 = (UINT32 *)ds_source;

	if (ds_colmapstyle == TC_COLORMAPSTYLE_8BPP)
		colormap = ds_colormap;
	else if (ds_colmapstyle == TC_COLORMAPSTYLE_32BPP)
		colormapu32 = (UINT32 *)ds_colormap;

	dest = (UINT32 *)(ylookup[ds_y] + columnofs[ds_x1]);

	if (ds_picfmt == PICFMT_FLAT)
	{
		if (ds_colmapstyle == TC_COLORMAPSTYLE_8BPP)
		{
			while (count >= 8)
			{
				// SoM: Why didn't I see this earlier? the spot variable is a waste now because we don't
				// have the uber complicated math to calculate it now, so that was a memory write we didn't
				// need!
				//
				// <Callum> 4194303 = (2048x2048)-1 (2048x2048 is maximum flat size)
				val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift);
				val &= 4194303;
				val = source[val];
				if (val != TRANSPARENTPIXEL)
					dest[0] = GetTrueColor(colormap[val]);
				xposition += xstep;
				yposition += ystep;

				val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift);
				val &= 4194303;
				val = source[val];
				if (val != TRANSPARENTPIXEL)
					dest[1] = GetTrueColor(colormap[val]);
				xposition += xstep;
				yposition += ystep;

				val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift);
				val &= 4194303;
				val = source[val];
				if (val != TRANSPARENTPIXEL)
					dest[2] = GetTrueColor(colormap[val]);
				xposition += xstep;
				yposition += ystep;

				val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift);
				val &= 4194303;
				val = source[val];
				if (val != TRANSPARENTPIXEL)
					dest[3] = GetTrueColor(colormap[val]);
				xposition += xstep;
				yposition += ystep;

				val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift);
				val &= 4194303;
				val = source[val];
				if (val != TRANSPARENTPIXEL)
					dest[4] = GetTrueColor(colormap[val]);
				xposition += xstep;
				yposition += ystep;

				val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift);
				val &= 4194303;
				val = source[val];
				if (val != TRANSPARENTPIXEL)
					dest[5] = GetTrueColor(colormap[val]);
				xposition += xstep;
				yposition += ystep;

				val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift);
				val &= 4194303;
				val = source[val];
				if (val != TRANSPARENTPIXEL)
					dest[6] = GetTrueColor(colormap[val]);
				xposition += xstep;
				yposition += ystep;

				val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift);
				val &= 4194303;
				val = source[val];
				if (val != TRANSPARENTPIXEL)
					dest[7] = GetTrueColor(colormap[val]);
				xposition += xstep;
				yposition += ystep;

				dest += 8;
				count -= 8;
			}
			while (count-- && dest <= deststop)
			{
				val = source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
				if (val != TRANSPARENTPIXEL)
					*dest = GetTrueColor(colormap[val]);
				dest++;
				xposition += xstep;
				yposition += ystep;
			}
		}
		else if (ds_colmapstyle == TC_COLORMAPSTYLE_32BPP)
		{
			while (count >= 8)
			{
				// SoM: Why didn't I see this earlier? the spot variable is a waste now because we don't
				// have the uber complicated math to calculate it now, so that was a memory write we didn't
				// need!
				//
				// <Callum> 4194303 = (2048x2048)-1 (2048x2048 is maximum flat size)
				val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift);
				val &= 4194303;
				val = source[val];
				if (val != TRANSPARENTPIXEL)
					dest[0] = (colormapu32[val]);
				xposition += xstep;
				yposition += ystep;

				val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift);
				val &= 4194303;
				val = source[val];
				if (val != TRANSPARENTPIXEL)
					dest[1] = (colormapu32[val]);
				xposition += xstep;
				yposition += ystep;

				val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift);
				val &= 4194303;
				val = source[val];
				if (val != TRANSPARENTPIXEL)
					dest[2] = (colormapu32[val]);
				xposition += xstep;
				yposition += ystep;

				val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift);
				val &= 4194303;
				val = source[val];
				if (val != TRANSPARENTPIXEL)
					dest[3] = (colormapu32[val]);
				xposition += xstep;
				yposition += ystep;

				val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift);
				val &= 4194303;
				val = source[val];
				if (val != TRANSPARENTPIXEL)
					dest[4] = (colormapu32[val]);
				xposition += xstep;
				yposition += ystep;

				val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift);
				val &= 4194303;
				val = source[val];
				if (val != TRANSPARENTPIXEL)
					dest[5] = (colormapu32[val]);
				xposition += xstep;
				yposition += ystep;

				val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift);
				val &= 4194303;
				val = source[val];
				if (val != TRANSPARENTPIXEL)
					dest[6] = (colormapu32[val]);
				xposition += xstep;
				yposition += ystep;

				val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift);
				val &= 4194303;
				val = source[val];
				if (val != TRANSPARENTPIXEL)
					dest[7] = (colormapu32[val]);
				xposition += xstep;
				yposition += ystep;

				dest += 8;
				count -= 8;
			}
			while (count-- && dest <= deststop)
			{
				val = source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
				if (val != TRANSPARENTPIXEL)
					*dest = (colormapu32[val]);
				dest++;
				xposition += xstep;
				yposition += ystep;
			}
		}
	}
	else if (ds_picfmt == PICFMT_FLAT32)
	{
		while (count >= 8)
		{
			// SoM: Why didn't I see this earlier? the spot variable is a waste now because we don't
			// have the uber complicated math to calculate it now, so that was a memory write we didn't
			// need!
			//
			// <Callum> 4194303 = (2048x2048)-1 (2048x2048 is maximum flat size)
			val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift);
			val &= 4194303;
			val = sourceu32[val];
			if (R_GetRgbaA(val))
				dest[0] = TC_ColorMix(val, dest[0]);
			xposition += xstep;
			yposition += ystep;

			val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift);
			val &= 4194303;
			val = sourceu32[val];
			if (R_GetRgbaA(val))
				dest[1] = TC_ColorMix(val, dest[1]);
			xposition += xstep;
			yposition += ystep;

			val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift);
			val &= 4194303;
			val = sourceu32[val];
			if (R_GetRgbaA(val))
				dest[2] = TC_ColorMix(val, dest[2]);
			xposition += xstep;
			yposition += ystep;

			val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift);
			val &= 4194303;
			val = sourceu32[val];
			if (R_GetRgbaA(val))
				dest[3] = TC_ColorMix(val, dest[3]);
			xposition += xstep;
			yposition += ystep;

			val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift);
			val &= 4194303;
			val = sourceu32[val];
			if (R_GetRgbaA(val))
				dest[4] = TC_ColorMix(val, dest[4]);
			xposition += xstep;
			yposition += ystep;

			val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift);
			val &= 4194303;
			val = sourceu32[val];
			if (R_GetRgbaA(val))
				dest[5] = TC_ColorMix(val, dest[5]);
			xposition += xstep;
			yposition += ystep;

			val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift);
			val &= 4194303;
			val = sourceu32[val];
			if (R_GetRgbaA(val))
				dest[6] = TC_ColorMix(val, dest[6]);
			xposition += xstep;
			yposition += ystep;

			val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift);
			val &= 4194303;
			val = sourceu32[val];
			if (R_GetRgbaA(val))
				dest[7] = TC_ColorMix(val, dest[7]);
			xposition += xstep;
			yposition += ystep;

			dest += 8;
			count -= 8;
		}
		while (count-- && dest <= deststop)
		{
			val = sourceu32[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
			if (R_GetRgbaA(val))
				*dest = TC_ColorMix(val, *dest);
			dest++;
			xposition += xstep;
			yposition += ystep;
		}
	}
}

/**	\brief The R_DrawTranslucentSplat_32 function
	Just like R_DrawSplat_32, but is translucent!
*/
void R_DrawTranslucentSplat_32 (void)
{
	fixed_t xposition;
	fixed_t yposition;
	fixed_t xstep, ystep;

	UINT8 *source;
	UINT32 *sourceu32;

	UINT8 *colormap = NULL;
	UINT32 *colormapu32 = NULL;

	UINT32 *dest;
	const UINT32 *deststop = (UINT32 *)screens[0] + vid.width * vid.height;

	size_t count = (ds_x2 - ds_x1 + 1);
	UINT32 val;

	xposition = ds_xfrac; yposition = ds_yfrac;
	xstep = ds_xstep; ystep = ds_ystep;

	// SoM: we only need 6 bits for the integer part (0 thru 63) so the rest
	// can be used for the fraction part. This allows calculation of the memory address in the
	// texture with two shifts, an OR and one AND. (see below)
	// for texture sizes > 64 the amount of precision we can allow will decrease, but only by one
	// bit per power of two (obviously)
	// Ok, because I was able to eliminate the variable spot below, this function is now FASTER
	// than the original span renderer. Whodathunkit?
	xposition <<= nflatshiftup; yposition <<= nflatshiftup;
	xstep <<= nflatshiftup; ystep <<= nflatshiftup;

	source = ds_source;
	sourceu32 = (UINT32 *)ds_source;

	if (ds_colmapstyle == TC_COLORMAPSTYLE_8BPP)
		colormap = ds_colormap;
	else if (ds_colmapstyle == TC_COLORMAPSTYLE_32BPP)
		colormapu32 = (UINT32 *)ds_colormap;

	dest = (UINT32 *)(ylookup[ds_y] + columnofs[ds_x1]);

	if (ds_picfmt == PICFMT_FLAT)
	{
		if (ds_colmapstyle == TC_COLORMAPSTYLE_8BPP)
		{
			while (count >= 8)
			{
				// SoM: Why didn't I see this earlier? the spot variable is a waste now because we don't
				// have the uber complicated math to calculate it now, so that was a memory write we didn't
				// need!
				val = source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
				if (val != TRANSPARENTPIXEL)
					WriteTranslucentSpanIdx(colormap[val], 0);
				xposition += xstep;
				yposition += ystep;

				val = source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
				if (val != TRANSPARENTPIXEL)
					WriteTranslucentSpanIdx(colormap[val], 1);
				xposition += xstep;
				yposition += ystep;

				val = source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
				if (val != TRANSPARENTPIXEL)
					WriteTranslucentSpanIdx(colormap[val], 2);
				xposition += xstep;
				yposition += ystep;

				val = source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
				if (val != TRANSPARENTPIXEL)
					WriteTranslucentSpanIdx(colormap[val], 3);
				xposition += xstep;
				yposition += ystep;

				val = source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
				if (val != TRANSPARENTPIXEL)
					WriteTranslucentSpanIdx(colormap[val], 4);
				xposition += xstep;
				yposition += ystep;

				val = source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
				if (val != TRANSPARENTPIXEL)
					WriteTranslucentSpanIdx(colormap[val], 5);
				xposition += xstep;
				yposition += ystep;

				val = source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
				if (val != TRANSPARENTPIXEL)
					WriteTranslucentSpanIdx(colormap[val], 6);
				xposition += xstep;
				yposition += ystep;

				val = source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
				if (val != TRANSPARENTPIXEL)
					WriteTranslucentSpanIdx(colormap[val], 7);
				xposition += xstep;
				yposition += ystep;

				dest += 8;
				count -= 8;
			}
			while (count-- && dest <= deststop)
			{
				val = source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
				if (val != TRANSPARENTPIXEL)
					WriteTranslucentSpan(colormap[val]);
				dest++;
				xposition += xstep;
				yposition += ystep;
			}
		}
		else if (ds_colmapstyle == TC_COLORMAPSTYLE_32BPP)
		{
			while (count >= 8)
			{
				// SoM: Why didn't I see this earlier? the spot variable is a waste now because we don't
				// have the uber complicated math to calculate it now, so that was a memory write we didn't
				// need!
				val = source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
				if (val != TRANSPARENTPIXEL)
					WriteTranslucentSpanIdx32(colormapu32[val], 0);
				xposition += xstep;
				yposition += ystep;

				val = source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
				if (val != TRANSPARENTPIXEL)
					WriteTranslucentSpanIdx32(colormapu32[val], 1);
				xposition += xstep;
				yposition += ystep;

				val = source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
				if (val != TRANSPARENTPIXEL)
					WriteTranslucentSpanIdx32(colormapu32[val], 2);
				xposition += xstep;
				yposition += ystep;

				val = source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
				if (val != TRANSPARENTPIXEL)
					WriteTranslucentSpanIdx32(colormapu32[val], 3);
				xposition += xstep;
				yposition += ystep;

				val = source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
				if (val != TRANSPARENTPIXEL)
					WriteTranslucentSpanIdx32(colormapu32[val], 4);
				xposition += xstep;
				yposition += ystep;

				val = source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
				if (val != TRANSPARENTPIXEL)
					WriteTranslucentSpanIdx32(colormapu32[val], 5);
				xposition += xstep;
				yposition += ystep;

				val = source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
				if (val != TRANSPARENTPIXEL)
					WriteTranslucentSpanIdx32(colormapu32[val], 6);
				xposition += xstep;
				yposition += ystep;

				val = source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
				if (val != TRANSPARENTPIXEL)
					WriteTranslucentSpanIdx32(colormapu32[val], 7);
				xposition += xstep;
				yposition += ystep;

				dest += 8;
				count -= 8;
			}
			while (count-- && dest <= deststop)
			{
				val = source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
				if (val != TRANSPARENTPIXEL)
					WriteTranslucentSpan32(colormapu32[val]);
				dest++;
				xposition += xstep;
				yposition += ystep;
			}
		}
	}
	else if (ds_picfmt == PICFMT_FLAT32)
	{
		while (count >= 8)
		{
			// SoM: Why didn't I see this earlier? the spot variable is a waste now because we don't
			// have the uber complicated math to calculate it now, so that was a memory write we didn't
			// need!
			val = sourceu32[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
			if (R_GetRgbaA(val))
				dest[0] = TC_TranslucentColorMix(val, dest[0], ds_alpha);
			xposition += xstep;
			yposition += ystep;

			val = sourceu32[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
			if (R_GetRgbaA(val))
				dest[1] = TC_TranslucentColorMix(val, dest[1], ds_alpha);
			xposition += xstep;
			yposition += ystep;

			val = sourceu32[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
			if (R_GetRgbaA(val))
				dest[2] = TC_TranslucentColorMix(val, dest[2], ds_alpha);
			xposition += xstep;
			yposition += ystep;

			val = sourceu32[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
			if (R_GetRgbaA(val))
				dest[3] = TC_TranslucentColorMix(val, dest[3], ds_alpha);
			xposition += xstep;
			yposition += ystep;

			val = sourceu32[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
			if (R_GetRgbaA(val))
				dest[4] = TC_TranslucentColorMix(val, dest[4], ds_alpha);
			xposition += xstep;
			yposition += ystep;

			val = sourceu32[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
			if (R_GetRgbaA(val))
				dest[5] = TC_TranslucentColorMix(val, dest[5], ds_alpha);
			xposition += xstep;
			yposition += ystep;

			val = sourceu32[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
			if (R_GetRgbaA(val))
				dest[6] = TC_TranslucentColorMix(val, dest[6], ds_alpha);
			xposition += xstep;
			yposition += ystep;

			val = sourceu32[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
			if (R_GetRgbaA(val))
				dest[7] = TC_TranslucentColorMix(val, dest[7], ds_alpha);
			xposition += xstep;
			yposition += ystep;

			dest += 8;
			count -= 8;
		}
		while (count-- && dest <= deststop)
		{
			val = sourceu32[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
			if (R_GetRgbaA(val))
				*dest = TC_TranslucentColorMix(val, *dest, ds_alpha);
			dest++;
			xposition += xstep;
			yposition += ystep;
		}
	}
}

/**	\brief The R_DrawTranslucentSpan_32 function
	Draws the actual span with translucency.
*/
void R_DrawTranslucentSpan_32 (void)
{
	fixed_t xposition;
	fixed_t yposition;
	fixed_t xstep, ystep;

	UINT8 *source;
	UINT32 *sourceu32;

	UINT8 *colormap = NULL;
	UINT32 *colormapu32 = NULL;

	UINT32 *dest;
	const UINT32 *deststop = (UINT32 *)screens[0] + vid.width * vid.height;

	size_t count = (ds_x2 - ds_x1 + 1);
	UINT32 val;

	xposition = ds_xfrac; yposition = ds_yfrac;
	xstep = ds_xstep; ystep = ds_ystep;

	// SoM: we only need 6 bits for the integer part (0 thru 63) so the rest
	// can be used for the fraction part. This allows calculation of the memory address in the
	// texture with two shifts, an OR and one AND. (see below)
	// for texture sizes > 64 the amount of precision we can allow will decrease, but only by one
	// bit per power of two (obviously)
	// Ok, because I was able to eliminate the variable spot below, this function is now FASTER
	// than the original span renderer. Whodathunkit?
	xposition <<= nflatshiftup; yposition <<= nflatshiftup;
	xstep <<= nflatshiftup; ystep <<= nflatshiftup;

	source = ds_source;
	sourceu32 = (UINT32 *)ds_source;

	if (ds_colmapstyle == TC_COLORMAPSTYLE_8BPP)
		colormap = ds_colormap;
	else if (ds_colmapstyle == TC_COLORMAPSTYLE_32BPP)
		colormapu32 = (UINT32 *)ds_colormap;

	dest = (UINT32 *)(ylookup[ds_y] + columnofs[ds_x1]);

	if (ds_picfmt == PICFMT_FLAT)
	{
		if (ds_colmapstyle == TC_COLORMAPSTYLE_8BPP)
		{
			while (count >= 8)
			{
				// SoM: Why didn't I see this earlier? the spot variable is a waste now because we don't
				// have the uber complicated math to calculate it now, so that was a memory write we didn't
				// need!
				WriteTranslucentSpanIdx(colormap[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]], 0);
				xposition += xstep;
				yposition += ystep;

				WriteTranslucentSpanIdx(colormap[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]], 1);
				xposition += xstep;
				yposition += ystep;

				WriteTranslucentSpanIdx(colormap[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]], 2);
				xposition += xstep;
				yposition += ystep;

				WriteTranslucentSpanIdx(colormap[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]], 3);
				xposition += xstep;
				yposition += ystep;

				WriteTranslucentSpanIdx(colormap[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]], 4);
				xposition += xstep;
				yposition += ystep;

				WriteTranslucentSpanIdx(colormap[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]], 5);
				xposition += xstep;
				yposition += ystep;

				WriteTranslucentSpanIdx(colormap[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]], 6);
				xposition += xstep;
				yposition += ystep;

				WriteTranslucentSpanIdx(colormap[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]], 7);
				xposition += xstep;
				yposition += ystep;

				dest += 8;
				count -= 8;
			}
			while (count-- && dest <= deststop)
			{
				val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift);
				WriteTranslucentSpan(colormap[source[val]]);
				dest++;
				xposition += xstep;
				yposition += ystep;
			}
		}
		else if (ds_colmapstyle == TC_COLORMAPSTYLE_32BPP)
		{
			while (count >= 8)
			{
				// SoM: Why didn't I see this earlier? the spot variable is a waste now because we don't
				// have the uber complicated math to calculate it now, so that was a memory write we didn't
				// need!
				WriteTranslucentSpanIdx32(colormapu32[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]], 0);
				xposition += xstep;
				yposition += ystep;

				WriteTranslucentSpanIdx32(colormapu32[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]], 1);
				xposition += xstep;
				yposition += ystep;

				WriteTranslucentSpanIdx32(colormapu32[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]], 2);
				xposition += xstep;
				yposition += ystep;

				WriteTranslucentSpanIdx32(colormapu32[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]], 3);
				xposition += xstep;
				yposition += ystep;

				WriteTranslucentSpanIdx32(colormapu32[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]], 4);
				xposition += xstep;
				yposition += ystep;

				WriteTranslucentSpanIdx32(colormapu32[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]], 5);
				xposition += xstep;
				yposition += ystep;

				WriteTranslucentSpanIdx32(colormapu32[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]], 6);
				xposition += xstep;
				yposition += ystep;

				WriteTranslucentSpanIdx32(colormapu32[source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]], 7);
				xposition += xstep;
				yposition += ystep;

				dest += 8;
				count -= 8;
			}
			while (count-- && dest <= deststop)
			{
				val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift);
				WriteTranslucentSpan32(colormapu32[source[val]]);
				dest++;
				xposition += xstep;
				yposition += ystep;
			}
		}
	}
	else if (ds_picfmt == PICFMT_FLAT32)
	{
		while (count >= 8)
		{
			// SoM: Why didn't I see this earlier? the spot variable is a waste now because we don't
			// have the uber complicated math to calculate it now, so that was a memory write we didn't
			// need!
			dest[0] = TC_TranslucentColorMix(sourceu32[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)], dest[0], ds_alpha);
			xposition += xstep;
			yposition += ystep;

			dest[1] = TC_TranslucentColorMix(sourceu32[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)], dest[1], ds_alpha);
			xposition += xstep;
			yposition += ystep;

			dest[2] = TC_TranslucentColorMix(sourceu32[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)], dest[2], ds_alpha);
			xposition += xstep;
			yposition += ystep;

			dest[3] = TC_TranslucentColorMix(sourceu32[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)], dest[3], ds_alpha);
			xposition += xstep;
			yposition += ystep;

			dest[4] = TC_TranslucentColorMix(sourceu32[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)], dest[4], ds_alpha);
			xposition += xstep;
			yposition += ystep;

			dest[5] = TC_TranslucentColorMix(sourceu32[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)], dest[5], ds_alpha);
			xposition += xstep;
			yposition += ystep;

			dest[6] = TC_TranslucentColorMix(sourceu32[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)], dest[6], ds_alpha);
			xposition += xstep;
			yposition += ystep;

			dest[7] = TC_TranslucentColorMix(sourceu32[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)], dest[7], ds_alpha);
			xposition += xstep;
			yposition += ystep;

			dest += 8;
			count -= 8;
		}
		while (count-- && dest <= deststop)
		{
			val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift);
			*dest = TC_TranslucentColorMix(sourceu32[val], *dest, ds_alpha);
			dest++;
			xposition += xstep;
			yposition += ystep;
		}
	}
}

#ifndef NOWATER
void R_DrawTranslucentWaterSpan_32(void)
{
	UINT32 xposition;
	UINT32 yposition;
	UINT32 xstep, ystep;

	UINT8 *source;
	UINT32 *sourceu32;

	UINT8 *colormap = NULL;
	UINT32 *colormapu32 = NULL;

	UINT32 *dest;
	UINT32 *dsrc;

	size_t count;

	// SoM: we only need 6 bits for the integer part (0 thru 63) so the rest
	// can be used for the fraction part. This allows calculation of the memory address in the
	// texture with two shifts, an OR and one AND. (see below)
	// for texture sizes > 64 the amount of precision we can allow will decrease, but only by one
	// bit per power of two (obviously)
	// Ok, because I was able to eliminate the variable spot below, this function is now FASTER
	// than the original span renderer. Whodathunkit?
	xposition = ds_xfrac << nflatshiftup; yposition = (ds_yfrac + ds_waterofs) << nflatshiftup;
	xstep = ds_xstep << nflatshiftup; ystep = ds_ystep << nflatshiftup;

	source = ds_source;
	sourceu32 = (UINT32 *)ds_source;

	if (ds_colmapstyle == TC_COLORMAPSTYLE_8BPP)
		colormap = ds_colormap;
	else if (ds_colmapstyle == TC_COLORMAPSTYLE_32BPP)
		colormapu32 = (UINT32 *)ds_colormap;

	dest = (UINT32 *)(ylookup[ds_y] + columnofs[ds_x1]);
	dsrc = ((UINT32 *)screens[1]) + (ds_y+ds_bgofs)*vid.width + ds_x1;
	count = ds_x2 - ds_x1 + 1;

	if (ds_picfmt == PICFMT_FLAT)
	{
		if (ds_colmapstyle == TC_COLORMAPSTYLE_8BPP)
		{
			while (count >= 8)
			{
				// SoM: Why didn't I see this earlier? the spot variable is a waste now because we don't
				// have the uber complicated math to calculate it now, so that was a memory write we didn't
				// need!
				WriteTranslucentWaterSpanIdx(colormap[source[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)]], 0);
				xposition += xstep;
				yposition += ystep;

				WriteTranslucentWaterSpanIdx(colormap[source[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)]], 1);
				xposition += xstep;
				yposition += ystep;

				WriteTranslucentWaterSpanIdx(colormap[source[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)]], 2);
				xposition += xstep;
				yposition += ystep;

				WriteTranslucentWaterSpanIdx(colormap[source[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)]], 3);
				xposition += xstep;
				yposition += ystep;

				WriteTranslucentWaterSpanIdx(colormap[source[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)]], 4);
				xposition += xstep;
				yposition += ystep;

				WriteTranslucentWaterSpanIdx(colormap[source[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)]], 5);
				xposition += xstep;
				yposition += ystep;

				WriteTranslucentWaterSpanIdx(colormap[source[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)]], 6);
				xposition += xstep;
				yposition += ystep;

				WriteTranslucentWaterSpanIdx(colormap[source[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)]], 7);
				xposition += xstep;
				yposition += ystep;

				dest += 8;
				count -= 8;
			}
			while (count--)
			{
				WriteTranslucentWaterSpan(colormap[source[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)]])
				dest++;
				xposition += xstep;
				yposition += ystep;
			}
		}
		else if (ds_colmapstyle == TC_COLORMAPSTYLE_32BPP)
		{
			while (count >= 8)
			{
				// SoM: Why didn't I see this earlier? the spot variable is a waste now because we don't
				// have the uber complicated math to calculate it now, so that was a memory write we didn't
				// need!
				WriteTranslucentWaterSpanIdx32(colormapu32[source[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)]], 0);
				xposition += xstep;
				yposition += ystep;

				WriteTranslucentWaterSpanIdx32(colormapu32[source[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)]], 1);
				xposition += xstep;
				yposition += ystep;

				WriteTranslucentWaterSpanIdx32(colormapu32[source[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)]], 2);
				xposition += xstep;
				yposition += ystep;

				WriteTranslucentWaterSpanIdx32(colormapu32[source[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)]], 3);
				xposition += xstep;
				yposition += ystep;

				WriteTranslucentWaterSpanIdx32(colormapu32[source[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)]], 4);
				xposition += xstep;
				yposition += ystep;

				WriteTranslucentWaterSpanIdx32(colormapu32[source[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)]], 5);
				xposition += xstep;
				yposition += ystep;

				WriteTranslucentWaterSpanIdx32(colormapu32[source[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)]], 6);
				xposition += xstep;
				yposition += ystep;

				WriteTranslucentWaterSpanIdx32(colormapu32[source[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)]], 7);
				xposition += xstep;
				yposition += ystep;

				dest += 8;
				count -= 8;
			}
			while (count--)
			{
				WriteTranslucentWaterSpan32(colormapu32[source[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)]])
				dest++;
				xposition += xstep;
				yposition += ystep;
			}
		}
	}
	else if (ds_picfmt == PICFMT_FLAT32)
	{
		while (count >= 8)
		{
			// SoM: Why didn't I see this earlier? the spot variable is a waste now because we don't
			// have the uber complicated math to calculate it now, so that was a memory write we didn't
			// need!
			dest[0] = TC_TranslucentColorMix(sourceu32[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)], *dsrc++, ds_alpha);
			xposition += xstep;
			yposition += ystep;

			dest[1] = TC_TranslucentColorMix(sourceu32[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)], *dsrc++, ds_alpha);
			xposition += xstep;
			yposition += ystep;

			dest[2] = TC_TranslucentColorMix(sourceu32[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)], *dsrc++, ds_alpha);
			xposition += xstep;
			yposition += ystep;

			dest[3] = TC_TranslucentColorMix(sourceu32[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)], *dsrc++, ds_alpha);
			xposition += xstep;
			yposition += ystep;

			dest[4] = TC_TranslucentColorMix(sourceu32[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)], *dsrc++, ds_alpha);
			xposition += xstep;
			yposition += ystep;

			dest[5] = TC_TranslucentColorMix(sourceu32[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)], *dsrc++, ds_alpha);
			xposition += xstep;
			yposition += ystep;

			dest[6] = TC_TranslucentColorMix(sourceu32[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)], *dsrc++, ds_alpha);
			xposition += xstep;
			yposition += ystep;

			dest[7] = TC_TranslucentColorMix(sourceu32[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)], *dsrc++, ds_alpha);
			xposition += xstep;
			yposition += ystep;

			dest += 8;
			count -= 8;
		}
		while (count--)
		{
			*dest = TC_TranslucentColorMix(sourceu32[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)], *dsrc++, ds_alpha);
			dest++;
			xposition += xstep;
			yposition += ystep;
		}
	}
}
#endif

/**	\brief The R_DrawFogSpan_32 function
	Draws the actual span with fogging.
*/
void R_DrawFogSpan_32(void)
{
	UINT32 *dest;
	RGBA_t pack;

	size_t count;

	//dest = ylookup[ds_y] + columnofs[ds_x1];
	dest = &topleft_u32[ds_y *vid.width + ds_x1];

	count = ds_x2 - ds_x1 + 1;

#define doblend(idx) pack.rgba = dest[idx]; dest[idx] = (0xFF000000 | TC_TintTrueColor(pack, (0xFF000000 | (UINT32)dp_extracolormap->rgba), R_GetRgbaA(dp_extracolormap->rgba) * 10));
	while (count >= 4)
	{
		doblend(0)
		doblend(1)
		doblend(2)
		doblend(3)

		dest += 4;
		count -= 4;
	}
#undef doblend

	while (count--)
	{
		pack.rgba = *dest;
		*dest = (0xFF000000 | TC_TintTrueColor(pack, (0xFF000000 | (UINT32)dp_extracolormap->rgba), R_GetRgbaA(dp_extracolormap->rgba) * 10));
		dest++;
	}
}

/**	\brief The R_DrawFogColumn_32 function
	Fog wall.
*/
void R_DrawFogColumn_32(void)
{
	INT32 count;
	UINT32 *dest;
	RGBA_t pack;

	count = dc_yh - dc_yl;

	// Zero length, column does not exceed a pixel.
	if (count < 0)
		return;

#ifdef RANGECHECK
	if ((unsigned)dc_x >= (unsigned)vid.width || dc_yl < 0 || dc_yh >= vid.height)
		I_Error("R_DrawFogColumn_32: %d to %d at %d", dc_yl, dc_yh, dc_x);
#endif

	// Framebuffer destination address.
	// Use ylookup LUT to avoid multiply with ScreenWidth.
	// Use columnofs LUT for subwindows?
	//dest = ylookup[dc_yl] + columnofs[dc_x];
	dest = &topleft_u32[dc_yl*vid.width + dc_x];

	// Determine scaling, which is the only mapping to be done.
	do
	{
		// Simple. Apply the colormap to what's already on the screen.
		pack.rgba = *dest;
		*dest = (0xFF000000 | TC_TintTrueColor(pack, (0xFF000000 | (UINT32)dp_extracolormap->rgba), R_GetRgbaA(dp_extracolormap->rgba) * 10));
		dest += vid.width;
	} while (count--);
}

/**	\brief The R_DrawShadeColumn_32 function
	This is for 3D floors that cast shadows on walls.

	This function just cuts the column up into sections and calls R_DrawColumn_32
*/
void R_DrawColumnShadowed_32(void)
{
	INT32 count, realyh, i, height, bheight = 0, solid = 0;

	realyh = dc_yh;

	count = dc_yh - dc_yl;

	// Zero length, column does not exceed a pixel.
	if (count < 0)
		return;

#ifdef RANGECHECK
	if ((unsigned)dc_x >= (unsigned)vid.width || dc_yl < 0 || dc_yh >= vid.height)
		I_Error("R_DrawColumnShadowed_32: %d to %d at %d", dc_yl, dc_yh, dc_x);
#endif

	// This runs through the lightlist from top to bottom and cuts up the column accordingly.
	for (i = 0; i < dc_numlights; i++)
	{
		// If the height of the light is above the column, get the colormap
		// anyway because the lighting of the top should be affected.
		solid = dc_lightlist[i].flags & FF_CUTSOLIDS;

		height = dc_lightlist[i].height >> LIGHTSCALESHIFT;
		if (solid)
		{
			bheight = dc_lightlist[i].botheight >> LIGHTSCALESHIFT;
			if (bheight < height)
			{
				// confounded slopes sometimes allow partial invertedness,
				// even including cases where the top and bottom heights
				// should actually be the same!
				// swap the height values as a workaround for this quirk
				INT32 temp = height;
				height = bheight;
				bheight = temp;
			}
		}
		if (height <= dc_yl)
		{
			dc_colormap = dc_lightlist[i].rcolormap;
			dp_extracolormap = dc_lightlist[i].extra_colormap;
			if (!dp_extracolormap)
				dp_extracolormap = defaultextracolormap;
			dp_lighting = dc_lightlist[i].blendlight;
			if (solid && dc_yl < bheight)
				dc_yl = bheight;
			continue;
		}
		// Found a break in the column!
		dc_yh = height;

		if (dc_yh > realyh)
			dc_yh = realyh;
		(colfuncs[BASEDRAWFUNC])();		// R_DrawColumn_32 for the appropriate architecture
		if (solid)
			dc_yl = bheight;
		else
			dc_yl = dc_yh + 1;

		dc_colormap = dc_lightlist[i].rcolormap;
		dp_extracolormap = dc_lightlist[i].extra_colormap;
		if (!dp_extracolormap)
			dp_extracolormap = defaultextracolormap;
		dp_lighting = dc_lightlist[i].blendlight;
	}
	dc_yh = realyh;
	if (dc_yl <= realyh)
		(colfuncs[BASEDRAWFUNC])();		// R_DrawWallColumn_32 for the appropriate architecture
}
