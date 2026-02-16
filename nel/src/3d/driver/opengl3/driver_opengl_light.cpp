// NeL - MMORPG Framework <http://dev.ryzom.com/projects/nel/>
// Copyright (C) 2010  Winch Gate Property Limited
//
// This source file has been modified by the following contributors:
// Copyright (C) 2013  Laszlo KIS-ADAM (dfighter) <dfighter1985@gmail.com>
// Copyright (C) 2014  Jan BOON (Kaetemi) <jan.boon@kaetemi.be>
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

#include "stdopengl.h"

#include "driver_opengl.h"
#include "driver_opengl_uniform_buffer.h"
#include "nel/3d/light.h"

#include <cmath>

namespace NL3D {
namespace NLDRIVERGL3 {

// ***************************************************************************
uint	CDriverGL3::getMaxLight () const
{
	H_AUTO_OGL(CDriverGL3_getMaxLight)
	// return MaxLight=8.
	return MaxLight;
}


// ***************************************************************************
void	CDriverGL3::setLight (uint8 num, const CLight& light)
{
	H_AUTO_OGL(CDriverGL3_setLight)
	// bkup real light, for lightmap dynamic lighting purpose
	if (num==0)
	{
		_UserLight0= light;
		// because the GL setup change, must dirt lightmap rendering
		_LightMapDynamicLightDirty= true;
	}

	setLightInternal(num, light);
}


CLight	CDriverGL3::getLight (uint8 num)
{
	return _UserLight[ num ];
}


// ***************************************************************************
void	CDriverGL3::setLightInternal(uint8 num, const CLight& light)
{
	H_AUTO_OGL(CDriverGL3_setLightInternal)
	// Check light count is good
	// nlassert(num < MaxLight);

	// Set the light
	if (num < MaxLight)
	{
		// GL light number
		GLenum lightNum = (GLenum)(GL_LIGHT0+num);

		// Get light mode
		CLight::TLightMode mode = light.getMode ();

		// Copy the mode
		_LightMode[num] = mode;
		_UserLight[num] = light;
		touchLightVP(num);

		// Set the position
		if ((mode == CLight::DirectionalLight) || (mode == CLight::SpotLight))
		{
			// Get the direction of the light
			_WorldLightDirection[num] = light.getDirection();
		}

		if (mode != CLight::DirectionalLight)
		{
			// Get the position of the light
			_WorldLightPos[num] = light.getPosition();
		}

		if (mode == CLight::SpotLight)
		{
			// Get the exponent of the spot
			float exponent = light.getExponent();

			// Get the cutoff of the spot
			float cutoff = 180.f * (light.getCutoff() / (float)NLMISC::Pi);

		}

	}
}

// ***************************************************************************
void	CDriverGL3::enableLight(uint8 num, bool enable)
{
	H_AUTO_OGL(CDriverGL3_enableLight)

	// User call => set the User flag (preserved across setupLightMapDynamicLighting)
	if (num < MaxLight)
		_UserLightEnable[num] = enable;

	// enable the light internally
	enableLightInternal(num, enable);

	// because the GL setup has changed, must dirt lightmap rendering
	_LightMapDynamicLightDirty = true;
}

bool	CDriverGL3::isLightEnabled (uint8 num)
{
	if (num < MaxLight)
		return _UserLightEnable[num];

	return false;
}


// ***************************************************************************
void	CDriverGL3::enableLightInternal(uint8 num, bool enable)
{
	H_AUTO_OGL(CDriverGL3_enableLightInternal)
	// Check light count is good
	// nlassert(num < MaxLight);

	// Set internal/active light enable state (analogous to _DriverGLStates.enableLight in original GL driver).
	// Does NOT modify _UserLightEnable — that's only set by the public enableLight() API.
	if (num < MaxLight)
	{
		_LightEnable[num] = enable;
		touchLightVP(num);
	}
}


// ***************************************************************************
void	CDriverGL3::enableLightTableMode(bool enable)
{
	H_AUTO_OGL(CDriverGL3_enableLightTableMode)
	_LightTableMode = enable;
}

// ***************************************************************************
void	CDriverGL3::setLightTableSize(uint count)
{
	H_AUTO_OGL(CDriverGL3_setLightTableSize)
	_LightTable.resize(count);
	_LightTableDirty = true;
}

// ***************************************************************************
void	CDriverGL3::setLightTableEntry(uint index, const CLight &light)
{
	H_AUTO_OGL(CDriverGL3_setLightTableEntry)
	if (index < _LightTable.size())
	{
		_LightTable[index] = light;
		_LightTableDirty = true;
	}
}

// ***************************************************************************
void	CDriverGL3::setLights(
	const sint16 *tableIndices,
	const uint8 *factors,
	uint numLights,
	NLMISC::CRGBA ambient)
{
	H_AUTO_OGL(CDriverGL3_setLights)

	// Clear all lights
	for (uint i = 0; i < MaxLight; ++i)
	{
		_LightEnable[i] = false;
		_LightTableObjIndices[i] = -1;
		_LightTableObjFactors[i] = 0.0f;
	}

	// Process each light (up to MaxLight)
	uint count = std::min(numLights, (uint)MaxLight);
	_LightTableObjCount = count;
	for (uint i = 0; i < count; ++i)
	{
		sint16 tableIndex = tableIndices[i];
		if (tableIndex < 0 || tableIndex >= (sint16)_LightTable.size())
			continue;

		// Store raw index and factor for UBO path
		_LightTableObjIndices[i] = tableIndex;
		_LightTableObjFactors[i] = (float)(factors[i] + (factors[i] >> 7)) / 256.0f;

		// Get raw light from table
		const CLight &rawLight = _LightTable[tableIndex];

		// Apply factor modulation (for legacy uniform path)
		uint ufactor = factors[i];
		ufactor += ufactor >> 7; // expand 0..255 to 0..256

		CRGBA slotAmbient;
		CRGBA diffuse, specular;

		if (i == 0)
			slotAmbient = ambient;
		else
			slotAmbient = CRGBA::Black;

		diffuse.modulateFromuiRGBOnly(rawLight.getDiffuse(), ufactor);
		specular.modulateFromuiRGBOnly(rawLight.getSpecular(), ufactor);

		// Build modulated CLight
		CLight::TLightMode mode = rawLight.getMode();
		_LightMode[i] = mode;

		if (mode == CLight::DirectionalLight)
		{
			_UserLight[i].setupDirectional(slotAmbient, diffuse, specular, rawLight.getDirection());
			_WorldLightDirection[i] = rawLight.getDirection();
		}
		else if (mode == CLight::SpotLight)
		{
			_UserLight[i].setupSpotLight(slotAmbient, diffuse, specular,
				rawLight.getPosition(), rawLight.getDirection(),
				rawLight.getExponent(), rawLight.getCutoff(),
				rawLight.getConstantAttenuation(),
				rawLight.getLinearAttenuation(),
				rawLight.getQuadraticAttenuation());
			_WorldLightPos[i] = rawLight.getPosition();
			_WorldLightDirection[i] = rawLight.getDirection();
		}
		else
		{
			_UserLight[i].setupPointLight(slotAmbient, diffuse, specular,
				rawLight.getPosition(), CVector::Null,
				rawLight.getConstantAttenuation(),
				rawLight.getLinearAttenuation(),
				rawLight.getQuadraticAttenuation());
			_WorldLightPos[i] = rawLight.getPosition();
		}

		_LightEnable[i] = true;
		touchLightVP(i);
	}

	// Touch remaining slots that were enabled before
	for (uint i = count; i < MaxLight; ++i)
	{
		touchLightVP(i);
	}

	// Synchronize user state for setupLightMapDynamicLighting restore
	for (uint i = 0; i < MaxLight; ++i)
		_UserLightEnable[i] = _LightEnable[i];
	if (count > 0)
		_UserLight0 = _UserLight[0];
	_LightMapDynamicLightDirty = true;
}


// ***************************************************************************

void	CDriverGL3::setAmbientColor (CRGBA color)
{
	H_AUTO_OGL(CDriverGL3_setAmbientColor)
	_AmbientGlobal = color;
}


// ***************************************************************************
void			CDriverGL3::setLightMapDynamicLight (bool enable, const CLight& light)
{
	H_AUTO_OGL(CDriverGL3_setLightMapDynamicLight)
	// just store, for future setup in lightmap material rendering
	_LightMapDynamicLightEnabled= enable;
	_LightMapDynamicLight= light;
	_LightMapDynamicLightDirty= true;
}


// ***************************************************************************
void			CDriverGL3::setupLightMapDynamicLighting(bool enable)
{
	H_AUTO_OGL(CDriverGL3_setupLightMapDynamicLighting)

	// start lightmap dynamic lighting
	if (enable)
	{
		// disable all lights but the 0th.
		for (uint i = 1; i < MaxLight; ++i)
			enableLightInternal(i, false);

		// if the dynamic light is really enabled
		if (_LightMapDynamicLightEnabled)
		{
			// then setup and enable
			setLightInternal(0, _LightMapDynamicLight);
			enableLightInternal(0, true);
		}
		// else just disable also the light 0
		else
		{
			enableLightInternal(0, false);
		}

		// ok it has been setup
		_LightMapDynamicLightDirty = false;
	}
	// restore old lighting
	else
	{
		// restore the light 0
		setLightInternal(0, _UserLight0);

		// restore all standard light enable states from _UserLightEnable
		// (which is never modified by enableLightInternal, only by the public enableLight API)
		for (uint i = 0; i < MaxLight; ++i)
			enableLightInternal(i, _UserLightEnable[i]);
	}
}

// ***************************************************************************
// CPU-side struct matching the std140 NlLightInfo layout (80 bytes)
struct CLightTableEntry
{
	float dirOrPos[3];     // 0
	sint32 mode;           // 12
	float diffuse[4];      // 16
	float specular[4];     // 32
	float constAttn;       // 48
	float linAttn;         // 52
	float quadAttn;        // 56
	float spotExp;         // 60
	float spotDir[3];      // 64
	float spotCutoff;      // 76
};                         // 80 bytes
static_assert(sizeof(CLightTableEntry) == 80, "CLightTableEntry must match std140 NlLightInfo layout");

void CDriverGL3::uploadLightTableUBO()
{
	H_AUTO_OGL(CDriverGL3_uploadLightTableUBO)

	if (!_LightTableDirty || !_LightTableUBOId)
		return;

	sint count = (sint)_LightTable.size();
	if (count == 0)
	{
		_LightTableDirty = false;
		return;
	}

	// Cap at max UBO size (128 lights × 80 bytes = 10240, fits in GL 3.3 min 16KB)
	const sint maxLights = 128;
	if (count > maxLights)
		count = maxLights;

	// Pack CLight data into UBO struct array
	std::vector<CLightTableEntry> entries(count);
	for (sint i = 0; i < count; ++i)
	{
		const CLight &light = _LightTable[i];
		CLightTableEntry &e = entries[i];

		CLight::TLightMode lmode = light.getMode();
		e.mode = (sint32)lmode;

		if (lmode == CLight::DirectionalLight)
		{
			CVector dir = light.getDirection();
			e.dirOrPos[0] = dir.x;
			e.dirOrPos[1] = dir.y;
			e.dirOrPos[2] = dir.z;
		}
		else
		{
			CVector pos = light.getPosition();
			e.dirOrPos[0] = pos.x;
			e.dirOrPos[1] = pos.y;
			e.dirOrPos[2] = pos.z;
		}

		NLMISC::CRGBAF diff(light.getDiffuse());
		e.diffuse[0] = diff.R;
		e.diffuse[1] = diff.G;
		e.diffuse[2] = diff.B;
		e.diffuse[3] = diff.A;

		NLMISC::CRGBAF spec(light.getSpecular());
		e.specular[0] = spec.R;
		e.specular[1] = spec.G;
		e.specular[2] = spec.B;
		e.specular[3] = spec.A;

		e.constAttn = light.getConstantAttenuation();
		e.linAttn = light.getLinearAttenuation();
		e.quadAttn = light.getQuadraticAttenuation();
		e.spotExp = light.getExponent();

		CVector spotDir = light.getDirection();
		e.spotDir[0] = spotDir.x;
		e.spotDir[1] = spotDir.y;
		e.spotDir[2] = spotDir.z;
		e.spotCutoff = cosf(light.getCutoff());
	}

	// Upload to GPU
	GLsizeiptr dataSize = count * sizeof(CLightTableEntry);
	nglBindBuffer(GL_UNIFORM_BUFFER, _LightTableUBOId);

	if (count > _LightTableUBOCapacity)
	{
		// Grow buffer
		nglBufferData(GL_UNIFORM_BUFFER, dataSize, &entries[0], GL_STREAM_DRAW);
		_LightTableUBOCapacity = count;
	}
	else
	{
		// Orphan + rewrite (avoids GPU sync)
		nglBufferData(GL_UNIFORM_BUFFER, _LightTableUBOCapacity * sizeof(CLightTableEntry), NULL, GL_STREAM_DRAW);
		nglBufferSubData(GL_UNIFORM_BUFFER, 0, dataSize, &entries[0]);
	}

	nglBindBuffer(GL_UNIFORM_BUFFER, 0);

	// Bind to the light table binding point
	nglBindBufferBase(GL_UNIFORM_BUFFER, NL_BUILTIN_LIGHT_TABLE_BINDING, _LightTableUBOId);

	_LightTableDirty = false;
}

// ***************************************************************************
void CDriverGL3::enableClipPlane(uint index, bool enable)
{
	H_AUTO_OGL(CDriverGL3_enableClipPlane)

	if (index >= MaxClipPlanes) return;

	_ClipPlaneEnabled[index] = enable;

	// Enable/disable GL clip distance (for builtin VPs that write gl_ClipDistance)
	if (enable)
		glEnable(GL_CLIP_DISTANCE0 + index);
	else
		glDisable(GL_CLIP_DISTANCE0 + index);

	// Trigger VP regeneration to include/exclude gl_ClipDistance output
	touchClipPlaneVP(index, enable);
}

// ***************************************************************************
void CDriverGL3::setClipPlane(uint index, const NLMISC::CPlane &plane)
{
	H_AUTO_OGL(CDriverGL3_setClipPlane)

	if (index >= MaxClipPlanes) return;

	// Plane is in NeL world space. Adjust d for _PZBCameraPos precision optimization,
	// then transform to eye space for the vertex shader.
	float pa = plane.a;
	float pb = plane.b;
	float pc = plane.c;
	float pd = plane.d + pa * _PZBCameraPos.x + pb * _PZBCameraPos.y + pc * _PZBCameraPos.z;

	// Transform plane from PZB-adjusted world space to eye space:
	// plane_eye = transpose(inverse(_ViewMtx)) * plane_pzb_world
	// For rigid body transform V: (V^-1)^T maps covariant vectors (planes/normals).
	CMatrix invView = _ViewMtx;
	invView.invert();
	CVector invI = invView.getI();
	CVector invJ = invView.getJ();
	CVector invK = invView.getK();
	CVector invPos = invView.getPos();

	_ClipPlaneEye[index][0] = invI.x * pa + invI.y * pb + invI.z * pc;
	_ClipPlaneEye[index][1] = invJ.x * pa + invJ.y * pb + invJ.z * pc;
	_ClipPlaneEye[index][2] = invK.x * pa + invK.y * pb + invK.z * pc;
	_ClipPlaneEye[index][3] = invPos.x * pa + invPos.y * pb + invPos.z * pc + pd;
}

} // NLDRIVERGL3
} // NL3D
