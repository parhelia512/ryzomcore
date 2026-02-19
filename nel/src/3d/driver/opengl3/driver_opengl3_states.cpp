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

#include "stdopengl3.h"

#include "driver_opengl3.h"

// ***************************************************************************
// define it For Debug purpose only. Normal use is to hide this line
// #define		NL3D_GLSTATE_DISABLE_CACHE

namespace NL3D {
namespace NLDRIVERGL3 {

// ***************************************************************************
CDriverGLStates3::CDriverGLStates3()
{
	H_AUTO_OGL(CDriverGLStates3_CDriverGLStates)

	// Initialize all cache members to GL defaults so the cache is never uninitialized.
	m_CurBlend = false;
	m_CurCullFace = true;
	m_CurZWrite = true;
	m_CurStencilTest = false;

	m_CurBlendSrc = GL_SRC_ALPHA;
	m_CurBlendDst = GL_ONE_MINUS_SRC_ALPHA;
	m_CurDepthFunc = GL_LEQUAL;
	m_CurStencilFunc = GL_ALWAYS;
	m_CurStencilRef = 0;
	m_CurStencilMask = std::numeric_limits<GLuint>::max();
	m_CurStencilOpFail = GL_KEEP;
	m_CurStencilOpZFail = GL_KEEP;
	m_CurStencilOpZPass = GL_KEEP;
	m_CurStencilWriteMask = std::numeric_limits<GLuint>::max();

	m_CurrentActiveTexture = 0;

	for (uint i = 0; i < CVertexBuffer::NumValue; ++i)
		m_VertexAttribArrayEnabled[i] = false;

	m_CurrARBVertexBuffer = 0;
	m_DepthRangeNear = 0.f;
	m_DepthRangeFar = 1.f;
	m_ZBias = 0.f;
	m_CullMode = CCW;
}

// ***************************************************************************
void CDriverGLStates3::init()
{
	H_AUTO_OGL(CDriverGLStates3_init)

	// Disable all vertex attrib arrays (GL state, not just cache).
	for (uint i = 0; i < CVertexBuffer::NumValue; ++i)
	{
		nglDisableVertexAttribArray(i);
		m_VertexAttribArrayEnabled[i] = false;
	}
}

// ***************************************************************************
void CDriverGLStates3::forceDefaults(uint /* nbStages */)
{
	H_AUTO_OGL(CDriverGLStates3_forceDefaults);

	// Enable / disable.
	m_CurBlend = false;
	m_CurCullFace = true;
	m_CurZWrite = true;
	m_CurStencilTest = false;

	glDisable(GL_BLEND);
	glEnable(GL_CULL_FACE);
	glDepthMask(GL_TRUE);
	glDisable(GL_STENCIL_TEST);

	// Blend, depth, and stencil functions.
	m_CurBlendSrc = GL_SRC_ALPHA;
	m_CurBlendDst = GL_ONE_MINUS_SRC_ALPHA;
	m_CurDepthFunc = GL_LEQUAL;
	m_CurStencilFunc = GL_ALWAYS;
	m_CurStencilRef = 0;
	m_CurStencilMask = std::numeric_limits<GLuint>::max();
	m_CurStencilOpFail = GL_KEEP;
	m_CurStencilOpZFail = GL_KEEP;
	m_CurStencilOpZPass = GL_KEEP;
	m_CurStencilWriteMask = std::numeric_limits<GLuint>::max();

	glBlendFunc(m_CurBlendSrc, m_CurBlendDst);
	glDepthFunc(m_CurDepthFunc);
	glStencilFunc(m_CurStencilFunc, m_CurStencilRef, m_CurStencilMask);
	glStencilOp(m_CurStencilOpFail, m_CurStencilOpZFail, m_CurStencilOpZPass);
	glStencilMask(m_CurStencilWriteMask);

	// Active texture to 0.
	nglActiveTexture(GL_TEXTURE0);
	m_CurrentActiveTexture = 0;

	// Depth range.
	m_DepthRangeNear = 0.f;
	m_DepthRangeFar = 1.f;
	m_ZBias = 0.f;
	glDepthRange(0, 1);

	// Cull order.
	m_CullMode = CCW;
	glCullFace(GL_BACK);
}

// ***************************************************************************
void CDriverGLStates3::enableBlend(uint enable)
{
	H_AUTO_OGL(CDriverGLStates3_enableBlend)
	// If different from current setup, update.
	bool enabled = (enable != 0);
#ifndef NL3D_GLSTATE_DISABLE_CACHE
	if (enabled != m_CurBlend)
#endif
	{
		// new state.
		m_CurBlend = enabled;
		// Setup GLState.
		if (m_CurBlend)
			glEnable(GL_BLEND);
		else
			glDisable(GL_BLEND);
	}
}

// ***************************************************************************
void CDriverGLStates3::enableCullFace(uint enable)
{
	H_AUTO_OGL(CDriverGLStates3_enableCullFace)
	// If different from current setup, update.
	bool enabled = (enable != 0);
#ifndef NL3D_GLSTATE_DISABLE_CACHE
	if (enabled != m_CurCullFace)
#endif
	{
		// new state.
		m_CurCullFace = enabled;
		// Setup GLState.
		if (m_CurCullFace)
			glEnable(GL_CULL_FACE);
		else
			glDisable(GL_CULL_FACE);
	}
}

// ***************************************************************************
void CDriverGLStates3::enableZWrite(uint enable)
{
	H_AUTO_OGL(CDriverGLStates3_enableZWrite)
	// If different from current setup, update.
	bool enabled = (enable != 0);
#ifndef NL3D_GLSTATE_DISABLE_CACHE
	if (enabled != m_CurZWrite)
#endif
	{
		// new state.
		m_CurZWrite = enabled;
		// Setup GLState.
		if (m_CurZWrite)
			glDepthMask(GL_TRUE);
		else
			glDepthMask(GL_FALSE);
	}
}

// ***************************************************************************
void CDriverGLStates3::enableStencilTest(bool enable)
{
	H_AUTO_OGL(CDriverGLStates3_enableStencilTest);

	// If different from current setup, update.
#ifndef NL3D_GLSTATE_DISABLE_CACHE
	if (enable != m_CurStencilTest)
#endif
	{
		// new state.
		m_CurStencilTest = enable;
		// Setup GLState.
		if (m_CurStencilTest)
			glEnable(GL_STENCIL_TEST);
		else
			glDisable(GL_STENCIL_TEST);
	}
}

// ***************************************************************************
void CDriverGLStates3::blendFunc(GLenum src, GLenum dst)
{
	H_AUTO_OGL(CDriverGLStates3_blendFunc)
	// If different from current setup, update.
#ifndef NL3D_GLSTATE_DISABLE_CACHE
	if (src != m_CurBlendSrc || dst != m_CurBlendDst)
#endif
	{
		// new state.
		m_CurBlendSrc = src;
		m_CurBlendDst = dst;
		// Setup GLState.
		glBlendFunc(m_CurBlendSrc, m_CurBlendDst);
	}
}

// ***************************************************************************
void CDriverGLStates3::depthFunc(GLenum zcomp)
{
	H_AUTO_OGL(CDriverGLStates3_depthFunc)
	// If different from current setup, update.
#ifndef NL3D_GLSTATE_DISABLE_CACHE
	if (zcomp != m_CurDepthFunc)
#endif
	{
		// new state.
		m_CurDepthFunc = zcomp;
		// Setup GLState.
		glDepthFunc(m_CurDepthFunc);
	}
}

// ***************************************************************************
void CDriverGLStates3::stencilFunc(GLenum func, GLint ref, GLuint mask)
{
	H_AUTO_OGL(CDriverGLStates3_stencilFunc)
#ifndef NL3D_GLSTATE_DISABLE_CACHE
	if ((func != m_CurStencilFunc) || (ref != m_CurStencilRef) || (mask != m_CurStencilMask))
#endif
	{
		// new state
		m_CurStencilFunc = func;
		m_CurStencilRef = ref;
		m_CurStencilMask = mask;

		// setup function.
		glStencilFunc(m_CurStencilFunc, m_CurStencilRef, m_CurStencilMask);
	}
}

// ***************************************************************************
void CDriverGLStates3::stencilOp(GLenum fail, GLenum zfail, GLenum zpass)
{
	H_AUTO_OGL(CDriverGLStates3_stencilOp)
#ifndef NL3D_GLSTATE_DISABLE_CACHE
	if ((fail != m_CurStencilOpFail) || (zfail != m_CurStencilOpZFail) || (zpass != m_CurStencilOpZPass))
#endif
	{
		// new state
		m_CurStencilOpFail = fail;
		m_CurStencilOpZFail = zfail;
		m_CurStencilOpZPass = zpass;

		// setup function.
		glStencilOp(m_CurStencilOpFail, m_CurStencilOpZFail, m_CurStencilOpZPass);
	}
}

// ***************************************************************************
void CDriverGLStates3::stencilMask(GLuint mask)
{
	H_AUTO_OGL(CDriverGLStates3_stencilMask)
#ifndef NL3D_GLSTATE_DISABLE_CACHE
	if (mask != m_CurStencilWriteMask)
#endif
	{
		// new state
		m_CurStencilWriteMask = mask;

		// setup function.
		glStencilMask(m_CurStencilWriteMask);
	}
}

// ***************************************************************************
static void convColor(CRGBA col, GLfloat glcol[4])
{
	H_AUTO_OGL(convColor)
	static const float OO255 = 1.0f / 255;
	glcol[0] = col.R * OO255;
	glcol[1] = col.G * OO255;
	glcol[2] = col.B * OO255;
	glcol[3] = col.A * OO255;
}

// ***************************************************************************
void CDriverGLStates3::updateDepthRange()
{
	H_AUTO_OGL(CDriverGLStates3_updateDepthRange);

	float delta = m_ZBias * (m_DepthRangeFar - m_DepthRangeNear);

	glDepthRange(delta + m_DepthRangeNear, delta + m_DepthRangeFar);
}

// ***************************************************************************
void CDriverGLStates3::setZBias(float zbias)
{
	H_AUTO_OGL(CDriverGLStates3_setZBias)
#ifndef NL3D_GLSTATE_DISABLE_CACHE
	if (zbias != m_ZBias)
#endif
	{
		m_ZBias = zbias;
		updateDepthRange();
	}
}

// ***************************************************************************
void CDriverGLStates3::setDepthRange(float znear, float zfar)
{
	H_AUTO_OGL(CDriverGLStates3_setDepthRange)
	nlassert(znear != zfar);
#ifndef NL3D_GLSTATE_DISABLE_CACHE
	if (znear != m_DepthRangeNear || zfar != m_DepthRangeFar)
#endif
	{
		m_DepthRangeNear = znear;
		m_DepthRangeFar = zfar;
		updateDepthRange();
	}
}

// ***************************************************************************
void CDriverGLStates3::activeTexture(uint stage)
{
	H_AUTO_OGL(CDriverGLStates3_activeTexture);

	if (m_CurrentActiveTexture != stage)
	{
		nglActiveTexture(GL_TEXTURE0 + stage);

		m_CurrentActiveTexture = stage;
	}
}

// ***************************************************************************
void CDriverGLStates3::forceActiveTexture(uint stage)
{
	H_AUTO_OGL(CDriverGLStates3_forceActiveTexture);

	nglActiveTexture(GL_TEXTURE0 + stage);

	m_CurrentActiveTexture = stage;
}

// ***************************************************************************
void CDriverGLStates3::enableVertexAttribArrayARB(uint glIndex, bool enable)
{
	H_AUTO_OGL(CDriverGLStates3_enableVertexAttribArrayARB);

#ifndef NL3D_GLSTATE_DISABLE_CACHE
	if (m_VertexAttribArrayEnabled[glIndex] != enable)
#endif
	{
		if (enable)
			nglEnableVertexAttribArray(glIndex);
		else
			nglDisableVertexAttribArray(glIndex);

		m_VertexAttribArrayEnabled[glIndex] = enable;
	}
}

// ***************************************************************************
void CDriverGLStates3::forceBindARBVertexBuffer(uint objectID)
{
	H_AUTO_OGL(CDriverGLStates3_forceBindARBVertexBuffer)

	nglBindBuffer(GL_ARRAY_BUFFER, objectID);

	m_CurrARBVertexBuffer = objectID;
}

// ***************************************************************************
void CDriverGLStates3::bindARBVertexBuffer(uint objectID)
{
	H_AUTO_OGL(CDriverGLStates3_bindARBVertexBuffer)
#ifndef NL3D_GLSTATE_DISABLE_CACHE
	if (objectID != m_CurrARBVertexBuffer)
#endif
	{
		forceBindARBVertexBuffer(objectID);
	}
}

// ***************************************************************************
void CDriverGLStates3::setCullMode(TCullMode cullMode)
{
	H_AUTO_OGL(CDriverGLStates3_setCullMode)
#ifndef NL3D_GLSTATE_DISABLE_CACHE
	if (cullMode != m_CullMode)
#endif
	{
		glCullFace(cullMode == CCW ? GL_BACK : GL_FRONT);
		m_CullMode = cullMode;
	}
}

// ***************************************************************************
CDriverGLStates3::TCullMode CDriverGLStates3::getCullMode() const
{
	H_AUTO_OGL(CDriverGLStates3_CDriverGLStates)
	return m_CullMode;
}

} // NLDRIVERGL3
} // NL3D
