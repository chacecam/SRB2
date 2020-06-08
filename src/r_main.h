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
/// \file  r_main.h
/// \brief Rendering variables, consvars, defines

#ifndef __R_MAIN__
#define __R_MAIN__

#include "d_player.h"
#include "r_data.h"

//
// POV related.
//
extern fixed_t viewcos, viewsin;
extern INT32 viewheight;
extern INT32 centerx, centery;

extern fixed_t centerxfrac, centeryfrac;
extern fixed_t projection, projectiony;
extern fixed_t fovtan; // field of view

extern size_t validcount, linecount, loopcount, framecount;

// Utility functions.
INT32 R_PointOnSide(fixed_t x, fixed_t y, node_t *node);
INT32 R_PointOnSegSide(fixed_t x, fixed_t y, seg_t *line);
angle_t R_PointToAngle(fixed_t x, fixed_t y);
angle_t R_PointToAngle2(fixed_t px2, fixed_t py2, fixed_t px1, fixed_t py1);
angle_t R_PointToAngleEx(INT32 x2, INT32 y2, INT32 x1, INT32 y1);
fixed_t R_PointToDist(fixed_t x, fixed_t y);
fixed_t R_PointToDist2(fixed_t px2, fixed_t py2, fixed_t px1, fixed_t py1);

subsector_t *R_PointInSubsector(fixed_t x, fixed_t y);
subsector_t *R_PointInSubsectorOrNull(fixed_t x, fixed_t y);

boolean R_DoCulling(line_t *cullheight, line_t *viewcullheight, fixed_t vz, fixed_t bottomh, fixed_t toph);

extern consvar_t cv_showhud, cv_translucenthud;
extern consvar_t cv_homremoval;
extern consvar_t cv_chasecam, cv_chasecam2;
extern consvar_t cv_flipcam, cv_flipcam2;

extern consvar_t cv_shadow;
extern consvar_t cv_translucency;
extern consvar_t cv_drawdist, cv_drawdist_nights, cv_drawdist_precip;
extern consvar_t cv_fov;
extern consvar_t cv_skybox;
extern consvar_t cv_tailspickup;

// Called by startup code.
void R_Init(void);

extern boolean setsizeneeded;
void R_ViewSizeChanged(void); // sets setsizeneeded true
void R_SetViewSize(void);

void R_SetupFrame(player_t *player);
void R_SkyboxFrame(player_t *player);

// Main rendering functions
extern void (*R_RenderPlayerView)(player_t *player);

// Set function pointers for rendering
void R_SetRenderFuncs(void);

// add commands related to engine, at game startup
void R_RegisterEngineStuff(void);
#endif
