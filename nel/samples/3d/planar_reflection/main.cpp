// NeL - MMORPG Framework <https://wiki.ryzom.dev/>
// Copyright (C) 2025  Jan BOON (Kaetemi) <jan.boon@kaetemi.be>
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

//
// Planar Reflection Demo
//
// Draws a rotating cube above a reflective floor at Z=0.
// The reflection is rendered from a mirrored camera into a render target,
// then projected onto the floor using CFrustum::project() for UV mapping.
//

#include <nel/misc/types_nl.h>
#include <nel/misc/app_context.h>
#include <nel/misc/common.h>
#include <nel/misc/debug.h>
#include <nel/misc/event_listener.h>
#include <nel/misc/geom_ext.h>
#include <nel/misc/plane.h>
#include <nel/misc/time_nl.h>

#include <nel/3d/u_driver.h>
#include <nel/3d/u_material.h>
#include <nel/3d/frustum.h>
#include <nel/3d/driver_user.h>
#include <nel/3d/texture_user.h>
#include <nel/3d/render_target_manager.h>

#ifdef NL_OS_WINDOWS
#ifndef NL_COMP_MINGW
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace std;
using namespace NLMISC;
using namespace NL3D;

// Build a view matrix from eye position, target, and up vector
static CMatrix buildViewMatrix(const CVector &eye, const CVector &target, const CVector &up)
{
	CVector jj = (target - eye).normed();
	CVector ii = (jj ^ up).normed();
	CVector kk = ii ^ jj;
	CMatrix camWorld;
	camWorld.setRot(ii, jj, kk, true);
	camWorld.setPos(eye);
	CMatrix viewMatrix = camWorld;
	viewMatrix.invert();
	return viewMatrix;
}

// Draw a colored quad (one face of the cube)
static void drawFace(UDriver *driver, UMaterial &mat,
	const CVector &v0, const CVector &v1, const CVector &v2, const CVector &v3,
	CRGBA color)
{
	CQuadColor quad;
	quad.V0 = v0;
	quad.V1 = v1;
	quad.V2 = v2;
	quad.V3 = v3;
	quad.Color0 = quad.Color1 = quad.Color2 = quad.Color3 = color;
	driver->drawQuad(quad, mat);
}

// Draw a cube centered at origin with given half-size, transformed by matrix
static void drawCube(UDriver *driver, UMaterial &mat, const CMatrix &transform, float s)
{
	// 8 vertices of the cube in NeL coords (X right, Y forward, Z up)
	CVector v[8] = {
		transform * CVector(-s, -s, -s), // 0: left  back  bottom
		transform * CVector( s, -s, -s), // 1: right back  bottom
		transform * CVector( s,  s, -s), // 2: right front bottom
		transform * CVector(-s,  s, -s), // 3: left  front bottom
		transform * CVector(-s, -s,  s), // 4: left  back  top
		transform * CVector( s, -s,  s), // 5: right back  top
		transform * CVector( s,  s,  s), // 6: right front top
		transform * CVector(-s,  s,  s), // 7: left  front top
	};

	// 6 faces with distinct colors
	drawFace(driver, mat, v[3], v[2], v[1], v[0], CRGBA(200,  50,  50)); // bottom (Z-) red
	drawFace(driver, mat, v[4], v[5], v[6], v[7], CRGBA( 50, 200,  50)); // top    (Z+) green
	drawFace(driver, mat, v[0], v[1], v[5], v[4], CRGBA( 50,  50, 200)); // back   (Y-) blue
	drawFace(driver, mat, v[2], v[3], v[7], v[6], CRGBA(200, 200,  50)); // front  (Y+) yellow
	drawFace(driver, mat, v[3], v[0], v[4], v[7], CRGBA(200,  50, 200)); // left   (X-) magenta
	drawFace(driver, mat, v[1], v[2], v[6], v[5], CRGBA( 50, 200, 200)); // right  (X+) cyan
}

// Planar reflection demo application
class CPlanarReflectionDemo : public IEventListener
{
public:
	CPlanarReflectionDemo();
	~CPlanarReflectionDemo();
	void run();

	virtual void operator()(const CEvent &event) NL_OVERRIDE;

private:
	bool m_CloseWindow;
	UDriver *m_Driver;
	UMaterial m_CubeMat;
	UMaterial m_FloorMat;
};

CPlanarReflectionDemo::CPlanarReflectionDemo()
	: m_CloseWindow(false)
{
	m_Driver = UDriver::createDriver(0, false);
	if (!m_Driver)
	{
		nlerror("Failed to create driver");
		return;
	}

	m_Driver->EventServer.addListener(EventCloseWindowId, this);

	m_Driver->setDisplay(UDriver::CMode(800, 600, 32, true));
	m_Driver->setWindowTitle(ucstring("NeL Planar Reflection Demo"));

	// Opaque material for the cube
	m_CubeMat = m_Driver->createMaterial();
	m_CubeMat.initUnlit();
	m_CubeMat.setZWrite(true);
	m_CubeMat.setZFunc(UMaterial::lessequal);

	// Textured material for the reflective floor
	m_FloorMat = m_Driver->createMaterial();
	m_FloorMat.initUnlit();
	m_FloorMat.setZWrite(true);
	m_FloorMat.setZFunc(UMaterial::lessequal);
	m_FloorMat.setDoubleSided(true);
	m_FloorMat.setBlend(true);
	m_FloorMat.setBlendFunc(UMaterial::srcalpha, UMaterial::invsrcalpha);
}

CPlanarReflectionDemo::~CPlanarReflectionDemo()
{
	m_FloorMat.setTexture(0, NULL);
	m_Driver->deleteMaterial(m_FloorMat);
	m_Driver->deleteMaterial(m_CubeMat);
	m_Driver->release();
	delete m_Driver;
}

void CPlanarReflectionDemo::operator()(const CEvent &event)
{
	if (event == EventCloseWindowId)
	{
		m_CloseWindow = true;
	}
}

void CPlanarReflectionDemo::run()
{
	CFrustum frustum;
	frustum.initPerspective(float(Pi / 3.0), 800.f / 600.f, 0.1f, 100.f);

	float camDist = 8.f;
	float camHeight = 4.f;
	float cubeHeight = 2.f;
	float cubeHalfSize = 1.0f;
	float floorExtent = 6.f;

	// CDriverUser provides setRenderTarget, which is not on the UDriver interface
	CDriverUser *dru = static_cast<CDriverUser *>(m_Driver);

	double startTime = CTime::ticksToSecond(CTime::getPerformanceTime());

	while (m_Driver->isActive() && !m_CloseWindow)
	{
		m_Driver->EventServer.pump();

		double now = CTime::ticksToSecond(CTime::getPerformanceTime()) - startTime;

		// --- Phase 1: Per-frame setup ---

		float camAngle = float(now * 0.3);
		CVector eye(cosf(camAngle) * camDist, sinf(camAngle) * camDist, camHeight);
		CVector target(0.f, 0.f, cubeHeight * 0.5f);
		CVector up(0.f, 0.f, 1.f);

		CMatrix viewMatrix = buildViewMatrix(eye, target, up);

		// Cube transform: positioned at cubeHeight, spinning
		CMatrix cubeTransform;
		cubeTransform.identity();
		cubeTransform.setPos(CVector(0.f, 0.f, cubeHeight));
		cubeTransform.rotateZ(float(now * 0.5));
		cubeTransform.rotateX(float(now * 0.3));

		CMatrix modelMatrix;
		modelMatrix.identity();

		// --- Phase 2: Reflected camera ---

		CVector reflectedEye(eye.x, eye.y, -eye.z);
		CVector reflectedTarget(target.x, target.y, -target.z);
		CMatrix reflectedViewMatrix = buildViewMatrix(reflectedEye, reflectedTarget, up);

		// --- Phase 3: Render reflected scene to render target ---

		CRenderTargetManager &rtm = m_Driver->getRenderTargetManager();
		CTextureUser *reflectRT = rtm.getRenderTarget(800, 600);

		dru->setRenderTarget(*reflectRT);

		m_Driver->clearBuffers(CRGBA(40, 40, 40));

		m_Driver->setFrustum(frustum);
		m_Driver->setViewMatrix(reflectedViewMatrix);
		m_Driver->setModelMatrix(modelMatrix);

		// Clip plane: keep geometry above Z=0
		m_Driver->setClipPlane(0, CPlane(0.f, 0.f, 1.f, 0.f));
		m_Driver->enableClipPlane(0, true);

		drawCube(m_Driver, m_CubeMat, cubeTransform, cubeHalfSize);

		m_Driver->enableClipPlane(0, false);

		// --- Phase 4: Unbind render target ---

		CTextureUser texNull;
		dru->setRenderTarget(texNull);

		// --- Phase 5: Render scene normally ---

		m_Driver->clearBuffers(CRGBA(40, 40, 40));

		m_Driver->setFrustum(frustum);
		m_Driver->setViewMatrix(viewMatrix);
		m_Driver->setModelMatrix(modelMatrix);

		drawCube(m_Driver, m_CubeMat, cubeTransform, cubeHalfSize);

		// --- Phase 6: Render floor with reflection texture ---

		// Set reflection texture on floor material
		m_FloorMat.setTexture(0, reflectRT);

		// Draw floor as a subdivided grid for correct projective UV mapping.
		// A single quad can't represent the nonlinear perspective projection;
		// subdividing gives each cell small enough UVs for linear interpolation.
		{
			const int gridN = 32;
			float step = (2.f * floorExtent) / float(gridN);
			CRGBA floorColor(150, 180, 220, 180); // slight blue tint, translucent

			for (int gy = 0; gy < gridN; ++gy)
			{
				for (int gx = 0; gx < gridN; ++gx)
				{
					float x0 = -floorExtent + gx * step;
					float x1 = x0 + step;
					float y0 = -floorExtent + gy * step;
					float y1 = y0 + step;

					CVector v0(x0, y0, 0.f);
					CVector v1(x1, y0, 0.f);
					CVector v2(x1, y1, 0.f);
					CVector v3(x0, y1, 0.f);

					CVector p0 = frustum.project(reflectedViewMatrix * v0);
					CVector p1 = frustum.project(reflectedViewMatrix * v1);
					CVector p2 = frustum.project(reflectedViewMatrix * v2);
					CVector p3 = frustum.project(reflectedViewMatrix * v3);

					CQuadColorUV q;
					q.V0 = v0; q.V1 = v1; q.V2 = v2; q.V3 = v3;
					q.Color0 = q.Color1 = q.Color2 = q.Color3 = floorColor;
					q.Uv0 = CUV(p0.x, p0.y);
					q.Uv1 = CUV(p1.x, p1.y);
					q.Uv2 = CUV(p2.x, p2.y);
					q.Uv3 = CUV(p3.x, p3.y);

					m_Driver->drawQuad(q, m_FloorMat);
				}
			}
		}

		// --- Phase 7: Cleanup and swap ---

		m_FloorMat.setTexture(0, NULL);
		rtm.recycleRenderTarget(reflectRT);

		m_Driver->swapBuffers();
	}
}

#ifdef NL_OS_WINDOWS
sint WINAPI WinMain(HINSTANCE /* hInstance */, HINSTANCE /* hPrevInstance */, LPSTR /* cmdline */, int /* nCmdShow */)
#else
sint main(int /* argc */, char ** /* argv */)
#endif
{
	CApplicationContext applicationContext;

	CPlanarReflectionDemo demo;
	demo.run();

	return EXIT_SUCCESS;
}
