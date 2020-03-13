// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 1998-2000 by DooM Legacy Team.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
//-----------------------------------------------------------------------------
/// \file
/// \brief imports/exports for the 3D hardware low-level interface API

#ifndef __HWR_DRV_H__
#define __HWR_DRV_H__

// this must be here 19991024 by Kin
#include "../screen.h"
#include "hw_data.h"
#include "hw_defs.h"
#include "hw_md2.h"

// ==========================================================================
//                                                       STANDARD DLL EXPORTS
// ==========================================================================

typedef void (*I_Error_t) (const char *error, ...) FUNCIERROR;
void DBG_Printf(const char *lpFmt, ...) /*FUNCPRINTF*/;

boolean HWD_Init(I_Error_t ErrorFunction);
#ifndef HAVE_SDL
void HWD_Shutdown(void);
#endif
#ifdef _WINDOWS
void HWD_GetModeList(vmode_t **pvidmodes, INT32 *numvidmodes);
#endif
void HWD_SetPalette(RGBA_t *ppal);
void HWD_FinishUpdate(INT32 waitvbl);
void HWD_Draw2DLine(F2DCoord *v1, F2DCoord *v2, RGBA_t Color);
void HWD_DrawPolygon(FSurfaceInfo *pSurf, FOutVector *pOutVerts, FUINT iNumPts, FBITFIELD PolyFlags);
void HWD_RenderSkyDome(INT32 tex, INT32 texture_width, INT32 texture_height, FTransform transform);
void HWD_SetBlend(FBITFIELD PolyFlags);
void HWD_ClearBuffer(FBOOLEAN ColorMask, FBOOLEAN DepthMask, FRGBAFloat *ClearColor);
void HWD_SetTexture(FTextureInfo *TexInfo);
void HWD_ReadRect(INT32 x, INT32 y, INT32 width, INT32 height, INT32 dst_stride, UINT16 *dst_data);
void HWD_GClipRect(INT32 minx, INT32 miny, INT32 maxx, INT32 maxy, float nearclip);
void HWD_ClearMipMapCache(void);

//Hurdler: added for backward compatibility
void HWD_SetSpecialState(hwdspecialstate_t IdState, INT32 Value);

//Hurdler: added for new development
void HWD_DrawModel(model_t *model, INT32 frameIndex, INT32 duration, INT32 tics, INT32 nextFrameIndex, FTransform *pos, float scale, UINT8 flipped, UINT8 *color);
void HWD_CreateModelVBOs(model_t *model);
void HWD_SetTransform(FTransform *ptransform);
INT32 HWD_GetTextureUsed(void);
INT32 HWD_GetRenderVersion(void);

#define SCREENVERTS 10
void HWD_PostImgRedraw(float points[SCREENVERTS][SCREENVERTS][2]);
void HWD_FlushScreenTextures(void);
void HWD_StartScreenWipe(void);
void HWD_EndScreenWipe(void);
void HWD_DoScreenWipe(void);
void HWD_DrawIntermissionBG(void);
void HWD_MakeScreenTexture(void);
void HWD_MakeScreenFinalTexture(void);
void HWD_DrawScreenFinalTexture(int width, int height);

#endif //__HWR_DRV_H__
