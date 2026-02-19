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

#ifndef NL_DRIVER_OPENGL3_STATES_H
#define NL_DRIVER_OPENGL3_STATES_H

#include "nel/misc/types_nl.h"
#include "nel/3d/vertex_buffer.h"

namespace NL3D {
namespace NLDRIVERGL3 {

// ***************************************************************************
/**
 * Class for optimizing GL3 core profile state calls by caching previous values.
 * All following GL calls must go through a single instance of this class:
 *   - glEnable() / glDisable(): GL_BLEND, GL_CULL_FACE, GL_STENCIL_TEST
 *   - glBlendFunc()
 *   - glDepthMask()
 *   - glDepthFunc()
 *   - glDepthRange()
 *   - glStencilFunc(), glStencilOp(), glStencilMask()
 *   - glCullFace()
 *   - glActiveTexture()
 *   - glEnableVertexAttribArray() / glDisableVertexAttribArray()
 *   - glBindBuffer(GL_ARRAY_BUFFER)
 */
class CDriverGLStates3
{
public:
	/// Constructor. no-op.
	CDriverGLStates3();
	// init. Do it just after setDisplay()
	void init();

	/// Reset all OpenGL states of interest to default, and update caching.
	void forceDefaults(uint nbTextureStages);

	/// \name enable if !0
	// @{
	void enableBlend(uint enable);
	void enableCullFace(uint enable);
	/// glDepthMask.
	void enableZWrite(uint enable);
	/// enable/disable stencil test
	void enableStencilTest(bool enable);
	bool isStencilTestEnabled() const { return m_CurStencilTest; }
	// @}

	/// glBlendFunc.
	void blendFunc(GLenum src, GLenum dst);
	/// glDepthFunc.
	void depthFunc(GLenum zcomp);
	/// glStencilFunc
	void stencilFunc(GLenum stencilFunc, GLint ref, GLuint mask);
	/// glStencilOp
	void stencilOp(GLenum fail, GLenum zfail, GLenum zpass);
	/// glStencilMask
	void stencilMask(uint mask);

	/// \name Depth range.
	// @{
	void setDepthRange(float znear, float zfar);
	void getDepthRange(float &znear, float &zfar) const
	{
		znear = m_DepthRangeNear;
		zfar = m_DepthRangeFar;
	}
	/** Set z-bias
	 * NB : this is done in window coordinate, not in world coordinate as with CMaterial
	 */
	void setZBias(float zbias);
	// @}

	/// \name Active texture unit.
	// @{
	/// glActiveTexture.
	void activeTexture(uint stage);
	/// glActiveTexture without cache check
	void forceActiveTexture(uint stage);
	/// get active texture
	uint getActiveTexture() const { return m_CurrentActiveTexture; }
	// @}

	/// glEnableVertexAttribArray / glDisableVertexAttribArray.
	void enableVertexAttribArrayARB(uint glIndex, bool enable);

	/// glBindBuffer(GL_ARRAY_BUFFER).
	void bindARBVertexBuffer(uint objectID);
	void forceBindARBVertexBuffer(uint objectID);
	uint getCurrBoundARBVertexBuffer() const { return m_CurrARBVertexBuffer; }

	enum TCullMode
	{
		CCW = 0,
		CW
	};
	void setCullMode(TCullMode cullMode);
	TCullMode getCullMode() const;

private:
	bool m_CurBlend;
	bool m_CurCullFace;
	bool m_CurZWrite;
	bool m_CurStencilTest;

	GLenum m_CurBlendSrc;
	GLenum m_CurBlendDst;
	GLenum m_CurDepthFunc;
	GLenum m_CurStencilFunc;
	GLint m_CurStencilRef;
	GLuint m_CurStencilMask;
	GLenum m_CurStencilOpFail;
	GLenum m_CurStencilOpZFail;
	GLenum m_CurStencilOpZPass;
	GLuint m_CurStencilWriteMask;

	uint m_CurrentActiveTexture;

	bool m_VertexAttribArrayEnabled[CVertexBuffer::NumValue];

	uint m_CurrARBVertexBuffer;

	float m_DepthRangeNear;
	float m_DepthRangeFar;
	float m_ZBias; // NB : zbias is in window coordinates

	TCullMode m_CullMode;

private:
	void updateDepthRange();
};

} // NLDRIVERGL3
} // NL3D

#endif // NL_DRIVER_OPENGL3_STATES_H

/* End of driver_opengl_states.h */
