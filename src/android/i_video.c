#include "../doomdef.h"
#include "../command.h"
#include "../i_video.h"
#include "../v_video.h"
#include "../screen.h"

#include "i_video.h"

#include "utils/Log.h"

rendermode_t rendermode = render_soft;
rendermode_t chosenrendermode = render_none;

boolean highcolor = false;

boolean allow_fullscreen = false;



consvar_t cv_vidwait = {"vid_wait", "On", CV_SAVE, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};

void I_StartupGraphics(void){}
void I_ShutdownGraphics(void){}

void I_StartupOpenGL(void){}

void I_SetPalette(RGBA_t *palette)
{
  (void)palette;
}

INT32 I_NumVideoModes(void)
{
  return 1;
}

INT32 I_GetVideoModeForSize(INT32 w, INT32 h)
{
  (void)w;
  (void)h;
  return 0;
}

INT32 I_SetVideoMode(INT32 modenum)
{
  vid.modenum = 0;
  vid.width = 320;
  vid.height = 240;
  vid.bpp = 1;
  vid.buffer = android_surface;
  return 0;
}

boolean I_CheckRenderer(void)
{
	return false;
}

void I_CheckGLLoaded(rendermode_t oldrender) {}

const char *I_GetVideoModeName(INT32 modenum)
{
  return "A320x240";
}

void I_UpdateNoBlit(void){}

void I_FinishUpdate(void) {
  LOGD("FRAME!");
  (*jni_env)->CallVoidMethod(jni_env, androidVideo, videoFrameCB);
}

void I_UpdateNoVsync(void) {}

void I_WaitVBL(INT32 count)
{
  (void)count;
}

void I_ReadScreen(UINT8 *scr)
{
  (void)scr;
}

void I_BeginRead(void){}

void I_EndRead(void){}
