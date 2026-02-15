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
// The floor polygon is tessellated using a screen-space grid with
// Sutherland-Hodgman clipping at the edges (matching the NeL water approach).
//
// Controls:
//   F - Toggle wireframe rendering
//   C - Toggle camera orbit
//   R - Toggle cube rotation
//   S - Toggle skybox roll
//

#include <nel/misc/types_nl.h>
#include <nel/misc/app_context.h>
#include <nel/misc/common.h>
#include <nel/misc/debug.h>
#include <nel/misc/event_listener.h>
#include <nel/misc/geom_ext.h>
#include <nel/misc/plane.h>
#include <nel/misc/polygon.h>
#include <nel/misc/time_nl.h>
#include <nel/misc/vector_2f.h>

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

// Un-project a screen-space grid coordinate to a world position on the Z=0 plane,
// and compute the reflection texture UV.
// Returns false if the ray doesn't hit the floor (pointing above horizon).
static bool gridToWorldAndUV(float gx, float gy, int gridN,
	const CFrustum &frustum, const CMatrix &camWorld, const CVector &eye,
	const CMatrix &reflectedViewMatrix,
	CVector &worldPos, CUV &uv)
{
	float sx = gx / float(gridN);
	float sy = gy / float(gridN);

	CVector nearLocal = frustum.unProject(CVector(sx, sy, 0.f));
	CVector worldNear = camWorld * nearLocal;
	CVector dir = worldNear - eye;

	if (dir.z >= -0.0001f)
		return false;

	float t = -eye.z / dir.z;
	worldPos.x = eye.x + t * dir.x;
	worldPos.y = eye.y + t * dir.y;
	worldPos.z = 0.f;

	CVector reflLocal = reflectedViewMatrix * worldPos;
	CVector proj = frustum.project(reflLocal);
	uv = CUV(proj.x, proj.y);
	return true;
}

// Emit a clipped polygon as triangle-fan quads.
// The polygon vertices are in grid coordinates; each is un-projected to Z=0
// and given a reflection UV. Quads are appended to the output vector.
static void emitClippedPoly(const CPolygon &clipPoly, int gridN,
	const CFrustum &frustum, const CMatrix &camWorld, const CVector &eye,
	const CMatrix &reflectedViewMatrix, CRGBA color,
	std::vector<CQuadColorUV> &outQuads)
{
	uint nv = (uint)clipPoly.Vertices.size();
	if (nv < 3) return;

	// Pre-compute world positions and UVs for all vertices
	std::vector<CVector> wp(nv);
	std::vector<CUV> uv(nv);
	for (uint i = 0; i < nv; ++i)
	{
		if (!gridToWorldAndUV(clipPoly.Vertices[i].x, clipPoly.Vertices[i].y, gridN,
			frustum, camWorld, eye, reflectedViewMatrix, wp[i], uv[i]))
			return;
	}

	// Convert triangle fan to quads: each quad covers 2 fan triangles
	for (uint k = 0; k < (nv - 1) / 2; ++k)
	{
		uint i0 = 0;
		uint i1 = 2 * k + 1;
		uint i2 = 2 * k + 2;
		uint i3 = (2 * k + 3 < nv) ? 2 * k + 3 : nv - 1;

		CQuadColorUV q;
		q.V0 = wp[i0]; q.V1 = wp[i1]; q.V2 = wp[i2]; q.V3 = wp[i3];
		q.Color0 = q.Color1 = q.Color2 = q.Color3 = color;
		q.Uv0 = uv[i0]; q.Uv1 = uv[i1]; q.Uv2 = uv[i2]; q.Uv3 = uv[i3];
		outQuads.push_back(q);
	}
}

// Draw a checkerboard skybox that rolls forward (rotates around X axis).
// Only the odd cells are drawn on top of the clear color.
static void drawSkybox(UDriver *driver, UMaterial &mat, const CVector &center, float angle)
{
	const float s = 50.f;
	const int grid = 16;
	float cs = 2.f * s / float(grid);
	CRGBA color(50, 50, 55);

	// Roll the skybox forward around the X axis
	CMatrix rot;
	rot.identity();
	rot.rotateX(angle);

	// Each face defined by origin corner (relative to center), U-axis, V-axis
	struct Face { CVector org, u, v; };
	Face faces[6] = {
		{ CVector(-s, -s,  s), CVector(1, 0, 0), CVector(0, 1, 0) }, // +Z top
		{ CVector(-s, -s, -s), CVector(1, 0, 0), CVector(0, 1, 0) }, // -Z bottom
		{ CVector( s, -s, -s), CVector(0, 1, 0), CVector(0, 0, 1) }, // +X right
		{ CVector(-s, -s, -s), CVector(0, 1, 0), CVector(0, 0, 1) }, // -X left
		{ CVector(-s,  s, -s), CVector(1, 0, 0), CVector(0, 0, 1) }, // +Y front
		{ CVector(-s, -s, -s), CVector(1, 0, 0), CVector(0, 0, 1) }, // -Y back
	};

	CQuadColor q;
	q.Color0 = q.Color1 = q.Color2 = q.Color3 = color;

	for (int f = 0; f < 6; ++f)
	{
		CVector org = center + rot * faces[f].org;
		CVector du = rot * (faces[f].u * cs);
		CVector dv = rot * (faces[f].v * cs);

		for (int j = 0; j < grid; ++j)
		{
			for (int i = 0; i < grid; ++i)
			{
				if ((i + j) % 2 == 0) continue;
				q.V0 = org + du * float(i)     + dv * float(j);
				q.V1 = org + du * float(i + 1) + dv * float(j);
				q.V2 = org + du * float(i + 1) + dv * float(j + 1);
				q.V3 = org + du * float(i)     + dv * float(j + 1);
				driver->drawQuad(q, mat);
			}
		}
	}
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
	bool m_Wireframe;
	bool m_AnimCamera;
	bool m_AnimCube;
	bool m_AnimSkybox;
	UDriver *m_Driver;
	UMaterial m_SkyMat;
	UMaterial m_CubeMat;
	UMaterial m_FloorMat;
};

CPlanarReflectionDemo::CPlanarReflectionDemo()
	: m_CloseWindow(false)
	, m_Wireframe(false)
	, m_AnimCamera(true)
	, m_AnimCube(true)
	, m_AnimSkybox(true)
{
	m_Driver = UDriver::createDriver(0, false);
	if (!m_Driver)
	{
		nlerror("Failed to create driver");
		return;
	}

	m_Driver->EventServer.addListener(EventCloseWindowId, this);
	m_Driver->EventServer.addListener(EventKeyDownId, this);

	m_Driver->setDisplay(UDriver::CMode(800, 600, 32, true));
	m_Driver->setWindowTitle(ucstring("NeL Planar Reflection Demo"));

	// Skybox material: no depth write, always passes depth test, double-sided
	m_SkyMat = m_Driver->createMaterial();
	m_SkyMat.initUnlit();
	m_SkyMat.setZWrite(false);
	m_SkyMat.setZFunc(UMaterial::always);
	m_SkyMat.setDoubleSided(true);

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
	m_Driver->deleteMaterial(m_SkyMat);
	m_Driver->release();
	delete m_Driver;
}

void CPlanarReflectionDemo::operator()(const CEvent &event)
{
	if (event == EventCloseWindowId)
	{
		m_CloseWindow = true;
	}
	else if (event == EventKeyDownId)
	{
		CEventKeyDown &keyDown = (CEventKeyDown &)event;
		if (keyDown.FirstTime)
		{
			if (keyDown.Key == KeyF) m_Wireframe = !m_Wireframe;
			if (keyDown.Key == KeyC) m_AnimCamera = !m_AnimCamera;
			if (keyDown.Key == KeyR) m_AnimCube = !m_AnimCube;
			if (keyDown.Key == KeyS) m_AnimSkybox = !m_AnimSkybox;
		}
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

	float camAngle = 0.f;
	float cubeAngleZ = 0.f;
	float cubeAngleX = 0.f;
	float skyAngle = 0.f;

	double lastTime = CTime::ticksToSecond(CTime::getPerformanceTime());

	while (m_Driver->isActive() && !m_CloseWindow)
	{
		m_Driver->EventServer.pump();

		double now = CTime::ticksToSecond(CTime::getPerformanceTime());
		float dt = float(now - lastTime);
		lastTime = now;

		if (m_AnimCamera) camAngle += dt * 0.3f;
		if (m_AnimCube) { cubeAngleZ += dt * 0.5f; cubeAngleX += dt * 0.3f; }
		if (m_AnimSkybox) skyAngle += dt * 0.1f;

		m_Driver->setPolygonMode(m_Wireframe ? UDriver::Line : UDriver::Filled);

		// --- Phase 1: Per-frame setup ---

		CVector eye(cosf(camAngle) * camDist, sinf(camAngle) * camDist, camHeight);
		CVector target(0.f, 0.f, cubeHeight * 0.5f);
		CVector up(0.f, 0.f, 1.f);

		CMatrix viewMatrix = buildViewMatrix(eye, target, up);

		// Cube transform: positioned at cubeHeight, spinning
		CMatrix cubeTransform;
		cubeTransform.identity();
		cubeTransform.setPos(CVector(0.f, 0.f, cubeHeight));
		cubeTransform.rotateZ(cubeAngleZ);
		cubeTransform.rotateX(cubeAngleX);

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

		drawSkybox(m_Driver, m_SkyMat, reflectedEye, skyAngle);
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

		drawSkybox(m_Driver, m_SkyMat, eye, skyAngle);
		drawCube(m_Driver, m_CubeMat, cubeTransform, cubeHalfSize);

		// --- Phase 6: Render floor with reflection texture ---

		// Set reflection texture on floor material
		m_FloorMat.setTexture(0, reflectRT);

		// Screen-space grid tessellation with polygon clipping
		// (matching the NeL water rendering approach):
		// 1. Project the floor polygon to screen-space grid coordinates
		// 2. Rasterize to classify grid cells as inner, border, or outer
		// 3. Clip border cells against the polygon edges (Sutherland-Hodgman)
		// 4. Un-project grid vertices back to Z=0, compute reflection UVs
		{
			const int gridN = 32;
			CRGBA floorColor(150, 180, 220, 180);

			CMatrix camWorld = viewMatrix;
			camWorld.invert();

			// Floor polygon: hexagon at Z=0
			const int numPolyVerts = 6;
			CVector2f floorPolyWorld[numPolyVerts];
			float radius = 5.f;
			for (int i = 0; i < numPolyVerts; ++i)
			{
				float a = float(i) * float(2.0 * Pi) / float(numPolyVerts);
				floorPolyWorld[i] = CVector2f(cosf(a) * radius, sinf(a) * radius);
			}

			// Project floor polygon to grid coordinates [0..gridN]
			CPolygon2D projPoly;
			projPoly.Vertices.resize(numPolyVerts);
			for (int i = 0; i < numPolyVerts; ++i)
			{
				CVector wp(floorPolyWorld[i].x, floorPolyWorld[i].y, 0.f);
				CVector scr = frustum.project(viewMatrix * wp);
				projPoly.Vertices[i] = CVector2f(scr.x * gridN, scr.y * gridN);
			}

			// Rasterize: find inner cells (fully inside) and outer cells (touched)
			CPolygon2D::TRasterVect inside;
			sint minYInside;
			projPoly.computeInnerBorders(inside, minYInside);

			CPolygon2D::TRasterVect border;
			sint minYBorder;
			projPoly.computeOuterBorders(border, minYBorder);

			// Build clip planes from projected polygon edges (in grid space, z=0)
			std::vector<CPlane> clipPlanes(numPolyVerts);
			bool ccw = projPoly.isCCWOriented();
			for (int i = 0; i < numPolyVerts; ++i)
			{
				int j = (i + 1) % numPolyVerts;
				CVector v0(projPoly.Vertices[i].x, projPoly.Vertices[i].y, 0.f);
				CVector v1(projPoly.Vertices[j].x, projPoly.Vertices[j].y, 0.f);
				CVector2f edge = projPoly.Vertices[j] - projPoly.Vertices[i];
				CVector v2(projPoly.Vertices[i].x, projPoly.Vertices[i].y, edge.norm());
				clipPlanes[i].make(v0, v1, v2);
				if (!ccw) clipPlanes[i].invert();
			}

			std::vector<CQuadColorUV> floorQuads;
			floorQuads.reserve(gridN * gridN);

			for (sint ky = 0; ky < (sint)border.size(); ++ky)
			{
				sint absY = minYBorder + ky;

				// Look up inner range for this scanline
				sint insideIdx = absY - minYInside;
				sint borderFirst = border[ky].first;
				sint borderLast = border[ky].second;
				sint insideFirst, insideLast;
				if (insideIdx >= 0 && insideIdx < (sint)inside.size()
					&& inside[insideIdx].first <= inside[insideIdx].second)
				{
					insideFirst = inside[insideIdx].first;
					insideLast = inside[insideIdx].second;
				}
				else
				{
					// No inside cells on this scanline — all cells are borders
					insideFirst = borderLast + 1;
					insideLast = borderLast;
				}

				// Left border cells: clip against polygon
				for (sint x = borderFirst; x < insideFirst; ++x)
				{
					CPolygon clipPoly;
					clipPoly.Vertices.resize(4);
					clipPoly.Vertices[0].set(float(x),     float(absY),     0.f);
					clipPoly.Vertices[1].set(float(x + 1), float(absY),     0.f);
					clipPoly.Vertices[2].set(float(x + 1), float(absY + 1), 0.f);
					clipPoly.Vertices[3].set(float(x),     float(absY + 1), 0.f);
					clipPoly.clip(clipPlanes);
					emitClippedPoly(clipPoly, gridN, frustum, camWorld, eye,
						reflectedViewMatrix, floorColor, floorQuads);
				}

				// Inner cells: no clipping needed
				for (sint x = insideFirst; x <= insideLast; ++x)
				{
					CVector wp[4];
					CUV uv[4];
					float coords[4][2] = {
						{ float(x),     float(absY) },
						{ float(x + 1), float(absY) },
						{ float(x + 1), float(absY + 1) },
						{ float(x),     float(absY + 1) }
					};
					bool valid = true;
					for (int c = 0; c < 4; ++c)
					{
						if (!gridToWorldAndUV(coords[c][0], coords[c][1], gridN,
							frustum, camWorld, eye, reflectedViewMatrix, wp[c], uv[c]))
						{
							valid = false;
							break;
						}
					}
					if (!valid) continue;

					CQuadColorUV q;
					q.V0 = wp[0]; q.V1 = wp[1]; q.V2 = wp[2]; q.V3 = wp[3];
					q.Color0 = q.Color1 = q.Color2 = q.Color3 = floorColor;
					q.Uv0 = uv[0]; q.Uv1 = uv[1]; q.Uv2 = uv[2]; q.Uv3 = uv[3];
					floorQuads.push_back(q);
				}

				// Right border cells: clip against polygon
				for (sint x = insideLast + 1; x <= borderLast; ++x)
				{
					CPolygon clipPoly;
					clipPoly.Vertices.resize(4);
					clipPoly.Vertices[0].set(float(x),     float(absY),     0.f);
					clipPoly.Vertices[1].set(float(x + 1), float(absY),     0.f);
					clipPoly.Vertices[2].set(float(x + 1), float(absY + 1), 0.f);
					clipPoly.Vertices[3].set(float(x),     float(absY + 1), 0.f);
					clipPoly.clip(clipPlanes);
					emitClippedPoly(clipPoly, gridN, frustum, camWorld, eye,
						reflectedViewMatrix, floorColor, floorQuads);
				}
			}

			if (!floorQuads.empty())
				m_Driver->drawQuads(floorQuads, m_FloorMat);
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
