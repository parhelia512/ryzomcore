// NeL - MMORPG Framework <http://dev.ryzom.com/projects/nel/>
// Copyright (C) 2010  Winch Gate Property Limited
//
// This source file has been modified by the following contributors:
// Copyright (C) 2019-2022  Jan BOON (Kaetemi) <jan.boon@kaetemi.be>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#ifndef NL_EXT_AL_H
#define NL_EXT_AL_H

#ifndef EFX_CREATIVE_AVAILABLE
#define EFX_CREATIVE_AVAILABLE 0
#endif

#ifndef EAX_AVAILABLE
#define EAX_AVAILABLE 0
#endif

#if EAX_AVAILABLE
#	define OPENAL
#	ifdef NL_OS_WINDOWS
#		include <objbase.h>
#	endif
#	include <eax.h>
#endif
#if EFX_CREATIVE_AVAILABLE
#	include <efx-creative.h>
#	include <EFX-Util.h>
#endif

#include <alext.h>

extern "C"
{

void alExtInit();
void alExtInitDevice(ALCdevice *device);

#if EAX_AVAILABLE
// EAX
extern bool AlExtEax;
extern EAXSet eaxSet;
extern EAXGet eaxGet;
#endif

// ALC_ENUMERATION_EXT
extern bool AlEnumerationExt;

// ALC_ENUMERATE_ALL_EXT
extern bool AlEnumerateAllExt;
#ifndef ALC_DEFAULT_ALL_DEVICES_SPECIFIER
#	define ALC_DEFAULT_ALL_DEVICES_SPECIFIER 0x1012
#endif
#ifndef ALC_ALL_DEVICES_SPECIFIER
#	define ALC_ALL_DEVICES_SPECIFIER 0x1013
#endif

// EAX-RAM (see OpenAL Programmer's Guide.pdf and http://icculus.org/alextreg/)
extern bool AlExtXRam;
typedef ALboolean (AL_APIENTRY *EAXSetBufferMode)(ALsizei n, ALuint *buffers, ALint value);
extern EAXSetBufferMode eaxSetBufferMode;
typedef ALenum (AL_APIENTRY *EAXGetBufferMode)(ALuint buffer, ALint *value);
extern EAXGetBufferMode eaxGetBufferMode;

// ALC_EXT_EFX (see Effects Extension Guide.pdf and http://icculus.org/alextreg/)
extern bool AlExtEfx;
#define ALC_MAX_AUXILIARY_SENDS 0x20003
typedef void (AL_APIENTRY *LPALGENEFXOBJECTS)(ALsizei n, ALuint* objects);
typedef void (AL_APIENTRY *LPALDELETEEFXOBJECTS)(ALsizei n, ALuint* objects);
typedef ALboolean (AL_APIENTRY *LPALISEFXOBJECT)(ALuint id);
typedef void (AL_APIENTRY *LPALEFXOBJECTI)(ALuint id, ALenum param, ALint value);
typedef void (AL_APIENTRY *LPALEFXOBJECTIV)(ALuint id, ALenum param, ALint* values);
typedef void (AL_APIENTRY *LPALEFXOBJECTF)(ALuint id, ALenum param, ALfloat value);
typedef void (AL_APIENTRY *LPALEFXOBJECTFV)(ALuint id, ALenum param, ALfloat* values);
typedef void (AL_APIENTRY *LPALGETEFXOBJECTI)(ALuint id, ALenum pname, ALint* value);
typedef void (AL_APIENTRY *LPALGETEFXOBJECTIV)(ALuint id, ALenum pname, ALint* values);
typedef void (AL_APIENTRY *LPALGETEFXOBJECTF)(ALuint id, ALenum pname, ALfloat* value);
typedef void (AL_APIENTRY *LPALGETEFXOBJECTFV)(ALuint id, ALenum pname, ALfloat* values);

#if !defined(AL_ALEXT_PROTOTYPES)
// effect objects
extern LPALGENEFXOBJECTS alGenEffects;
extern LPALDELETEEFXOBJECTS alDeleteEffects;
extern LPALISEFXOBJECT alIsEffect;
extern LPALEFXOBJECTI alEffecti;
extern LPALEFXOBJECTIV alEffectiv;
extern LPALEFXOBJECTF alEffectf;
extern LPALEFXOBJECTFV alEffectfv;
extern LPALGETEFXOBJECTI alGetEffecti;
extern LPALGETEFXOBJECTIV alGetEffectiv;
extern LPALGETEFXOBJECTF alGetEffectf;
extern LPALGETEFXOBJECTFV alGetEffectfv;

// filter objects
extern LPALGENEFXOBJECTS alGenFilters;
extern LPALDELETEEFXOBJECTS alDeleteFilters;
extern LPALISEFXOBJECT alIsFilter;
extern LPALEFXOBJECTI alFilteri;
extern LPALEFXOBJECTIV alFilteriv;
extern LPALEFXOBJECTF alFilterf;
extern LPALEFXOBJECTFV alFilterfv;
extern LPALGETEFXOBJECTI alGetFilteri;
extern LPALGETEFXOBJECTIV alGetFilteriv;
extern LPALGETEFXOBJECTF alGetFilterf;
extern LPALGETEFXOBJECTFV alGetFilterfv;

// submix objects
extern LPALGENEFXOBJECTS alGenAuxiliaryEffectSlots;
extern LPALDELETEEFXOBJECTS alDeleteAuxiliaryEffectSlots;
extern LPALISEFXOBJECT alIsAuxiliaryEffectSlot;
extern LPALEFXOBJECTI alAuxiliaryEffectSloti;
extern LPALEFXOBJECTIV alAuxiliaryEffectSlotiv;
extern LPALEFXOBJECTF alAuxiliaryEffectSlotf;
extern LPALEFXOBJECTFV alAuxiliaryEffectSlotfv;
extern LPALGETEFXOBJECTI alGetAuxiliaryEffectSloti;
extern LPALGETEFXOBJECTIV alGetAuxiliaryEffectSlotiv;
extern LPALGETEFXOBJECTF alGetAuxiliaryEffectSlotf;
extern LPALGETEFXOBJECTFV alGetAuxiliaryEffectSlotfv;
#endif

}

#endif /* #ifndef NL_EXT_AL_H */

/* end of file */
