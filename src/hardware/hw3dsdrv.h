// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 2001 by DooM Legacy Team.
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
/// \brief 3D sound import/export prototypes for low-level hardware interface

#ifndef __HW_3DS_DRV_H__
#define __HW_3DS_DRV_H__

// Use standart hardware API
#include "hws_data.h"

#if defined (HAVE_SDL) || !defined (HWD)
void HW3DS_Shutdown(void);
#endif

// Use standart Init and Shutdown functions

INT32  HW3DS_Startup(I_Error_t FatalErrorFunction, snddev_t *snd_dev);
u_int  HW3DS_AddSfx(sfx_data_t *sfx);
INT32  HW3DS_AddSource(source3D_data_t *src, u_int sfxhandle);
INT32  HW3DS_StartSource(INT32 handle);
void   HW3DS_StopSource(INT32 handle);
void   HW3DS_BeginFrameUpdate(void);
void   HW3DS_EndFrameUpdate(void);
INT32  HW3DS_IsPlaying(INT32 handle);
void   HW3DS_UpdateListener(listener_data_t *data, INT32 num);
void   HW3DS_UpdateSourceParms(INT32 handle, INT32 vol, INT32 sep);
void   HW3DS_SetGlobalSfxVolume(INT32 volume);
INT32  HW3DS_SetCone(INT32 handle, cone_def_t *cone_def);
void   HW3DS_Update3DSource(INT32 handle, source3D_pos_t *data);
INT32  HW3DS_ReloadSource(INT32 handle, u_int sfxhandle);
void   HW3DS_KillSource(INT32 handle);
void   HW3DS_KillSfx(u_int sfxhandle);
void   HW3DS_GetHW3DSTitle(char *buf, size_t size);

#endif // __HW_3DS_DRV_H__
