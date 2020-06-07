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

#include "hw_dll.h"

#define SCREENVERTS 10

// ==========================================================================
//                                                       STANDARD DLL EXPORTS
// ==========================================================================

void DBG_Printf(const char *lpFmt, ...) /*FUNCPRINTF*/;

boolean GL_Init(void);

#ifdef _WINDOWS
void GL_Shutdown(void);
void GL_GetModeList(vmode_t **pvidmodes, INT32 *numvidmodes);
#endif

boolean GL_Surface(INT32 w, INT32 h);
void GL_FinishUpdate(boolean waitvbl);

void GL_SetTransform(FTransform *ptransform);
void GL_SetModelView(int w, int h);
void GL_SetStates(void);
void GL_SetPalette(RGBA_t *ppal);

void GL_DrawPolygon(FSurfaceInfo *pSurf, FOutVector *pOutVerts, FUINT iNumPts, FBITFIELD PolyFlags);
void GL_Draw2DLine(F2DCoord *v1, F2DCoord *v2, RGBA_t Color);
void GL_RenderSkyDome(INT32 tex, INT32 texture_width, INT32 texture_height, FTransform transform);
void GL_DrawModel(model_t *model, INT32 frameIndex, INT32 duration, INT32 tics, INT32 nextFrameIndex, FTransform *pos, float scale, UINT8 flipped, UINT8 *color);
void GL_CreateModelVBOs(model_t *model);

void GL_SetTexture(FTextureInfo *TexInfo);
void GL_SetBlend(FBITFIELD PolyFlags);
void GL_SetSpecialState(hwdspecialstate_t IdState, INT32 Value);

void GL_ClearBuffer(FBOOLEAN ColorMask, FBOOLEAN DepthMask, FRGBAFloat *ClearColor);
void GL_ReadRect(INT32 x, INT32 y, INT32 width, INT32 height, INT32 dst_stride, UINT16 *dst_data);
void GL_GClipRect(INT32 minx, INT32 miny, INT32 maxx, INT32 maxy, float nearclip);

INT32 GL_GetTextureUsed(void);
void GL_ClearMipMapCache(void);
void GL_Flush(void);

void GL_PostImgRedraw(float points[SCREENVERTS][SCREENVERTS][2]);
void GL_FlushScreenTextures(void);
void GL_StartScreenWipe(void);
void GL_EndScreenWipe(void);
void GL_DoScreenWipe(void);
void GL_DrawIntermissionBG(void);
void GL_MakeScreenTexture(void);
void GL_MakeScreenFinalTexture(void);
void GL_DrawScreenFinalTexture(int width, int height);

#endif //__HWR_DRV_H__

