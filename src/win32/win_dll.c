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
//-----------------------------------------------------------------------------
/// \file
/// \brief load and initialise the 3D driver DLL

#include "../doomdef.h"
#ifdef HWRENDER
#include "../hardware/hw_drv.h"        // get the standard 3D Driver DLL exports prototypes
#endif

#ifdef HW3SOUND
#include "../hardware/hw3dsdrv.h"      // get the 3D sound driver DLL export prototypes
#endif

#ifdef _WINDOWS

#include "win_dll.h"
#include "win_main.h"       // I_ShowLastError()

#if defined(HWRENDER) || defined(HW3SOUND)
typedef struct loadfunc_s {
	LPCSTR fnName;
	LPVOID fnPointer;
} loadfunc_t;

// --------------------------------------------------------------------------
// Load a DLL, returns the HMODULE handle or NULL
// --------------------------------------------------------------------------
static HMODULE LoadDLL (LPCSTR dllName, loadfunc_t *funcTable)
{
	LPVOID      funcPtr;
	loadfunc_t *loadfunc;
	HMODULE     hModule;

	if ((hModule = LoadLibraryA(dllName)) != NULL)
	{
		// get function pointers for all functions we use
		for (loadfunc = funcTable; loadfunc->fnName != NULL; loadfunc++)
		{
			funcPtr = GetProcAddress(hModule, loadfunc->fnName);
			if (!funcPtr) {
				I_ShowLastError(FALSE);
				MessageBoxA(NULL, va("The '%s' haven't the good specification (function %s missing)\n\n"
				           "You must use dll from the same zip of this exe\n", dllName, loadfunc->fnName),
				           "Error", MB_OK|MB_ICONINFORMATION);
				return FALSE;
			}
			// store function address
			*((LPVOID*)loadfunc->fnPointer) = funcPtr;
		}
	}
	else
	{
		I_ShowLastError(FALSE);
		MessageBoxA(NULL, va("LoadLibrary() FAILED : couldn't load '%s'\r\n", dllName), "Warning", MB_OK|MB_ICONINFORMATION);
	}

	return hModule;
}


// --------------------------------------------------------------------------
// Unload the DLL
// --------------------------------------------------------------------------
static VOID UnloadDLL (HMODULE* pModule)
{
	if (FreeLibrary(*pModule))
		*pModule = NULL;
	else
		I_ShowLastError(TRUE);
}
#endif

// ==========================================================================
// STANDARD 3D DRIVER DLL FOR DOOM LEGACY
// ==========================================================================

// note : the 3D driver loading should be put somewhere else..

#ifdef HWRENDER
BOOL Init3DDriver (LPCSTR dllName)
{
	(void)dllName;
	return TRUE;
}

VOID Shutdown3DDriver (VOID)
{
	// :)
}
#endif

#ifdef HW3SOUND
BOOL Init3DSDriver(LPCSTR dllName)
{
	(void)dllName;
	return TRUE;
}

VOID Shutdown3DSDriver (VOID)
{
	// :)
}
#endif
#endif //_WINDOWS
