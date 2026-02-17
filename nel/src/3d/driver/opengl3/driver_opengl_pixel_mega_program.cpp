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

/**
 * Megashader Pixel Program
 *
 * Split vs Fold Design:
 *
 *   SPLITS (separate shader variants):
 *   - Fog (on/off): Separate pass (sky/UI vs world). Free split.
 *   - Cubemap (has any vs none): sampler2D vs samplerCube is a static GLSL
 *     type. Only Specular shader uses cubemaps, always a separate material
 *     group. In the cube variant, sampler1 is declared as samplerCube.
 *     Free split.
 *
 *   - Specular separate (on/off): Whether VP outputs specularColor varying.
 *     Mega VP always outputs it; user VPs may not. Free split.
 *
 *   Result: 8 PP variants — m_MegaPP[fog][cube][specular]
 *
 *   FOLDS (uniform-controlled branching — zero GPU cost):
 *   - Shader type (Normal/UserColor/Specular/LightMap): 4 short paths in
 *     a uniform switch on nlShader.
 *   - FogMode (Linear/Exp/Exp2): 3 one-line formulas, uniform switch.
 *   - AlphaTest: Uniform bool, one discard line.
 *   - TextureActive: Uniform bitmask, guards texture sampling per stage.
 *   - VertexFormat: Declare all varying inputs, only read what's needed.
 *   - TexEnvMode[4]: Packed uint32 per stage as uniforms. Arg selection and
 *     combine ops expressed as ternary chains (cmov/select, zero divergence).
 *     EMBM handled via conditional re-sample.
 */

#include "stdopengl.h"

#include <sstream>

#include "nel/3d/pixel_program.h"
#include "nel/3d/light.h"

#include "driver_opengl.h"
#include "driver_opengl_program.h"
#include "driver_opengl_vertex_buffer.h"

namespace NL3D {
namespace NLDRIVERGL3 {

namespace /* anonymous */ {

#define MEGA_PP_MAX_SAMPLERS 8

void megaPPGenerate(std::string &result, bool fog, bool cube, bool specular, bool cameraUBO, bool objectUBO, bool materialUBO)
{
	// Object UBO implies camera UBO
	if (objectUBO) { cameraUBO = true; }

	std::stringstream ss;
	ss << "// Megashader Pixel Program";
	ss << " (fog=" << (int)fog << ", cube=" << (int)cube << ", specular=" << (int)specular
	   << ", cameraUBO=" << (int)cameraUBO << ", objectUBO=" << (int)objectUBO
	   << ", materialUBO=" << (int)materialUBO << ")" << std::endl;
	ss << std::endl;
	ss << "#version 330" << std::endl;
	ss << "#extension GL_ARB_separate_shader_objects : enable" << std::endl;
	ss << std::endl;

	ss << "out vec4 fragColor;" << std::endl;
	ss << std::endl;

	// Varying inputs (all declared unconditionally)
	for (int i = Weight; i < NumOffsets; ++i)
	{
		if (i == PrimaryColor || i == SecondaryColor)
			continue;
		ss << "layout(location = " << i << ") smooth in vec4 " << g_AttribNames[i] << ";" << std::endl;
	}
	ss << std::endl;

	if (fog)
		ss << "layout(location = " << VaryingLocationEcPos << ") smooth in vec4 ecPos;" << std::endl;
	ss << "layout(location = " << VaryingLocationVertexColor << ") smooth in vec4 vertexColor;" << std::endl;
	if (specular)
		ss << "layout(location = " << VaryingLocationSpecularColor << ") smooth in vec4 specularColor;" << std::endl;
	ss << std::endl;

	// Samplers
	for (int i = 0; i < MEGA_PP_MAX_SAMPLERS; ++i)
	{
		if (cube && i == 1)
			ss << "uniform samplerCube sampler" << i << ";" << std::endl;
		else
			ss << "uniform sampler2D sampler" << i << ";" << std::endl;
	}
	ss << std::endl;

	// NlModel UBO block is provided by GLSLObjectHeader (for nlVertexFormat)
	// NlMaterial UBO block is provided by GLSLMaterialHeader (for materialColor, alphaRef, etc.)
	// NlCamera UBO block is provided by GLSLCameraHeader

	// Existing uniforms (skip when material UBO provides them)
	if (!materialUBO)
	{
		ss << "uniform vec4 materialColor;" << std::endl;
		ss << "uniform float alphaRef;" << std::endl;
	}
	ss << std::endl;

	// Per-stage constants and EMBM matrices (always individual uniforms)
	for (int i = 0; i < MEGA_PP_MAX_SAMPLERS; ++i)
		ss << "uniform vec4 constant" << i << ";" << std::endl;
	for (int i = 0; i < IDRV_MAT_MAXTEXTURES; ++i)
		ss << "uniform vec4 embmMatrix" << i << ";" << std::endl;
	ss << std::endl;

	// Fog uniforms (individual uniforms only when no camera UBO)
	if (fog && !cameraUBO)
	{
		ss << "uniform vec2 fogParams;" << std::endl;
		ss << "uniform vec4 fogColor;" << std::endl;
		ss << "uniform float fogDensity;" << std::endl;
		ss << "uniform vec3 cameraForward;" << std::endl;
		ss << std::endl;
	}

	// Megashader control uniforms (skip those in object/material UBO)
	if (!materialUBO)
	{
		ss << "uniform int nlShader;" << std::endl;
		ss << "uniform int nlTextureActive;" << std::endl;
		ss << "uniform uint nlTexEnvMode0;" << std::endl;
		ss << "uniform uint nlTexEnvMode1;" << std::endl;
		ss << "uniform uint nlTexEnvMode2;" << std::endl;
		ss << "uniform uint nlTexEnvMode3;" << std::endl;
		ss << "uniform int nlAlphaTest;" << std::endl;
	}
	if (!objectUBO)
		ss << "uniform int nlVertexFormat;" << std::endl;
	if (fog && !cameraUBO)
		ss << "uniform int nlFogMode;" << std::endl;
	if (fog && !objectUBO)
		ss << "uniform int nlWorldSpacePosition;" << std::endl;
	ss << std::endl;

	// Vertex format flag constants for texcoord availability
	for (int i = 0; i < IDRV_MAT_MAXTEXTURES; ++i)
		ss << "const int NL_PP_TEXCOORD" << i << "_FLAG = " << (int)g_VertexFlags[TexCoord0 + i] << ";" << std::endl;
	ss << std::endl;

	// --- TexEnv helper functions ---
	ss << "vec4 getTexEnvSrc(int src, vec4 texel, vec4 previous, vec4 diffuse, vec4 cnst) {" << std::endl;
	ss << "  return (src == 0) ? texel : (src == 1) ? previous : (src == 2) ? diffuse : cnst;" << std::endl;
	ss << "}" << std::endl;
	ss << std::endl;

	ss << "vec3 getArgRGB(int src, int op, vec4 texel, vec4 previous, vec4 diffuse, vec4 cnst) {" << std::endl;
	ss << "  vec4 s = getTexEnvSrc(src, texel, previous, diffuse, cnst);" << std::endl;
	ss << "  return (op == 0) ? s.rgb : (op == 1) ? (vec3(1.0) - s.rgb) : (op == 2) ? s.aaa : (vec3(1.0) - s.aaa);" << std::endl;
	ss << "}" << std::endl;
	ss << std::endl;

	ss << "float getArgA(int src, int op, vec4 texel, vec4 previous, vec4 diffuse, vec4 cnst) {" << std::endl;
	ss << "  vec4 s = getTexEnvSrc(src, texel, previous, diffuse, cnst);" << std::endl;
	ss << "  return (op == 0) ? s.r : (op == 1) ? (1.0 - s.r) : (op == 2) ? s.a : (1.0 - s.a);" << std::endl;
	ss << "}" << std::endl;
	ss << std::endl;

	ss << "vec3 combRGB(int op, vec3 a0, vec3 a1, vec3 a2, vec4 texel, vec4 prev, vec4 diff, vec4 cnst) {" << std::endl;
	ss << "  if (op == 0) return a0;" << std::endl; // Replace
	ss << "  if (op == 1) return a0 * a1;" << std::endl; // Modulate
	ss << "  if (op == 2) return a0 + a1;" << std::endl; // Add
	ss << "  if (op == 3) return a0 + a1 - vec3(0.5);" << std::endl; // AddSigned
	ss << "  float s;" << std::endl;
	ss << "  if (op == 4) { s = texel.a; return a0 * s + a1 * (1.0 - s); }" << std::endl; // InterpolateTexture
	ss << "  if (op == 5) { s = prev.a;  return a0 * s + a1 * (1.0 - s); }" << std::endl; // InterpolatePrevious
	ss << "  if (op == 6) { s = diff.a;  return a0 * s + a1 * (1.0 - s); }" << std::endl; // InterpolateDiffuse
	ss << "  if (op == 7) { s = cnst.a;  return a0 * s + a1 * (1.0 - s); }" << std::endl; // InterpolateConstant
	ss << "  if (op == 9) return a0 * a1 + a2;" << std::endl; // Mad
	ss << "  return a0;" << std::endl;
	ss << "}" << std::endl;
	ss << std::endl;

	ss << "float combA(int op, float a0, float a1, float a2, vec4 texel, vec4 prev, vec4 diff, vec4 cnst) {" << std::endl;
	ss << "  if (op == 0) return a0;" << std::endl;
	ss << "  if (op == 1) return a0 * a1;" << std::endl;
	ss << "  if (op == 2) return a0 + a1;" << std::endl;
	ss << "  if (op == 3) return a0 + a1 - 0.5;" << std::endl;
	ss << "  float s;" << std::endl;
	ss << "  if (op == 4) { s = texel.a; return a0 * s + a1 * (1.0 - s); }" << std::endl;
	ss << "  if (op == 5) { s = prev.a;  return a0 * s + a1 * (1.0 - s); }" << std::endl;
	ss << "  if (op == 6) { s = diff.a;  return a0 * s + a1 * (1.0 - s); }" << std::endl;
	ss << "  if (op == 7) { s = cnst.a;  return a0 * s + a1 * (1.0 - s); }" << std::endl;
	ss << "  if (op == 9) return a0 * a1 + a2;" << std::endl;
	ss << "  return a0;" << std::endl;
	ss << "}" << std::endl;
	ss << std::endl;

	// Apply one TexEnv stage (not EMBM — caller handles EMBM separately)
	ss << "vec4 applyTexEnv(uint env, vec4 texel, vec4 prev, vec4 diff, vec4 cnst) {" << std::endl;
	ss << "  int opRGB   = int(env & 0xFu);" << std::endl;
	ss << "  int src0RGB = int((env >>  4) & 0x3u);" << std::endl;
	ss << "  int op0RGB  = int((env >>  6) & 0x3u);" << std::endl;
	ss << "  int src1RGB = int((env >>  8) & 0x3u);" << std::endl;
	ss << "  int op1RGB  = int((env >> 10) & 0x3u);" << std::endl;
	ss << "  int src2RGB = int((env >> 12) & 0x3u);" << std::endl;
	ss << "  int op2RGB  = int((env >> 14) & 0x3u);" << std::endl;
	ss << "  int opA     = int((env >> 16) & 0xFu);" << std::endl;
	ss << "  int src0A   = int((env >> 20) & 0x3u);" << std::endl;
	ss << "  int op0A    = int((env >> 22) & 0x3u);" << std::endl;
	ss << "  int src1A   = int((env >> 24) & 0x3u);" << std::endl;
	ss << "  int op1A    = int((env >> 26) & 0x3u);" << std::endl;
	ss << "  int src2A   = int((env >> 28) & 0x3u);" << std::endl;
	ss << "  int op2A    = int((env >> 30) & 0x3u);" << std::endl;
	ss << "  vec3 a0r = getArgRGB(src0RGB, op0RGB, texel, prev, diff, cnst);" << std::endl;
	ss << "  vec3 a1r = getArgRGB(src1RGB, op1RGB, texel, prev, diff, cnst);" << std::endl;
	ss << "  vec3 a2r = getArgRGB(src2RGB, op2RGB, texel, prev, diff, cnst);" << std::endl;
	ss << "  float a0a = getArgA(src0A, op0A, texel, prev, diff, cnst);" << std::endl;
	ss << "  float a1a = getArgA(src1A, op1A, texel, prev, diff, cnst);" << std::endl;
	ss << "  float a2a = getArgA(src2A, op2A, texel, prev, diff, cnst);" << std::endl;
	ss << "  return vec4(combRGB(opRGB, a0r, a1r, a2r, texel, prev, diff, cnst)," << std::endl;
	ss << "              combA(opA, a0a, a1a, a2a, texel, prev, diff, cnst));" << std::endl;
	ss << "}" << std::endl;
	ss << std::endl;

	// Fog function
	if (fog)
	{
		ss << "vec4 applyFog(vec4 col) {" << std::endl;
		ss << "  float z;" << std::endl;
		ss << "  if (nlWorldSpacePosition != 0) {" << std::endl;
		if (cameraUBO)
			ss << "    vec3 camFwd = vec3(viewMatrix[0].y, viewMatrix[1].y, viewMatrix[2].y);" << std::endl;
		ss << "    z = abs(dot(ecPos.xyz, " << (cameraUBO ? "camFwd" : "cameraForward") << "));" << std::endl;
		ss << "  } else" << std::endl;
		ss << "    z = abs(ecPos.y / ecPos.w);" << std::endl;
		ss << "  float fogFactor;" << std::endl;
		ss << "  if (nlFogMode == 1) fogFactor = clamp(exp(-fogDensity * z), 0.0, 1.0);" << std::endl;
		ss << "  else if (nlFogMode == 2) fogFactor = clamp(exp(-fogDensity * fogDensity * z * z), 0.0, 1.0);" << std::endl;
		ss << "  else fogFactor = clamp((fogParams.t - z) / (fogParams.t - fogParams.s), 0.0, 1.0);" << std::endl;
		ss << "  vec4 fColor = mix(fogColor, col, fogFactor);" << std::endl;
		ss << "  fColor.a = col.a;" << std::endl;
		ss << "  return fColor;" << std::endl;
		ss << "}" << std::endl;
		ss << std::endl;
	}

	// --- main ---
	ss << "void main(void) {" << std::endl;
	ss << "  fragColor = vertexColor;" << std::endl;
	ss << std::endl;

	// Pre-sample all active textures
	for (int i = 0; i < MEGA_PP_MAX_SAMPLERS; ++i)
		ss << "  vec4 texel" << i << " = vec4(0.0);" << std::endl;
	ss << std::endl;

	// Sampling: shader-type-aware texcoord selection
	for (int i = 0; i < MEGA_PP_MAX_SAMPLERS; ++i)
	{
		ss << "  if ((nlTextureActive & " << (1 << i) << ") != 0) {" << std::endl;

		// UserColor: texel1 = texel0 (same texture sampled once)
		if (i == 1)
		{
			ss << "    if (nlShader == 1) { texel1 = texel0; } else {" << std::endl;
		}

		// Determine texcoord
		if (i < IDRV_MAT_MAXTEXTURES)
		{
			// Stages 0-3: Normal/UserColor/Specular use texCoordN, LightMap is special
			ss << "    vec2 tc;" << std::endl;
			if (i == 0)
			{
				// Stage 0: LightMap checks if more stages follow
				ss << "    if (nlShader == 3 && (nlTextureActive & " << (1 << (i + 1)) << ") != 0)" << std::endl;
				ss << "      tc = texCoord1.st;" << std::endl;
				ss << "    else" << std::endl;
				ss << "      tc = texCoord0.st;" << std::endl;
			}
			else
			{
				// Stage N: LightMap uses texCoord1 if more stages follow, else texCoord0
				ss << "    if (nlShader == 3) {" << std::endl;
				if (i < MEGA_PP_MAX_SAMPLERS - 1)
					ss << "      tc = ((nlTextureActive & " << (1 << (i + 1)) << ") != 0) ? texCoord1.st : texCoord0.st;" << std::endl;
				else
					ss << "      tc = texCoord0.st;" << std::endl;
				ss << "    } else {" << std::endl;
				// Non-lightmap: use texCoordN, fallback to texCoord0
				ss << "      tc = ((nlVertexFormat & NL_PP_TEXCOORD" << std::min(i, (int)IDRV_MAT_MAXTEXTURES - 1) << "_FLAG) != 0) ? texCoord" << i << ".st : texCoord0.st;" << std::endl;
				ss << "    }" << std::endl;
			}
		}
		else
		{
			// Stages 4-7: only used by LightMap
			ss << "    vec2 tc;" << std::endl;
			if (i < MEGA_PP_MAX_SAMPLERS - 1)
				ss << "    tc = ((nlTextureActive & " << (1 << (i + 1)) << ") != 0) ? texCoord1.st : texCoord0.st;" << std::endl;
			else
				ss << "    tc = texCoord0.st;" << std::endl;
		}

		// Sample
		if (cube && i == 1)
			ss << "    texel" << i << " = texture(sampler" << i << ", texCoord" << i << ".stp);" << std::endl;
		else
			ss << "    texel" << i << " = texture(sampler" << i << ", tc);" << std::endl;

		if (i == 1) // Close UserColor else block
			ss << "    }" << std::endl;

		ss << "  }" << std::endl;
	}
	ss << std::endl;

	// --- Shader switch ---
	// Normal (0) / UserColor (1): TexEnv interpreter
	ss << "  if (nlShader == 0 || nlShader == 1) {" << std::endl;
	ss << "    vec4 texEnvResult = fragColor;" << std::endl;
	ss << "    vec2 embmOfs = vec2(0.0);" << std::endl;
	ss << "    bool prevEMBM = false;" << std::endl;
	ss << std::endl;

	// Unrolled 4 TexEnv stages
	const char *envNames[] = { "nlTexEnvMode0", "nlTexEnvMode1", "nlTexEnvMode2", "nlTexEnvMode3" };
	const char *cnstNames[] = { "constant0", "constant1", "constant2", "constant3" };
	const char *embmNames[] = { "embmMatrix0", "embmMatrix1", "embmMatrix2", "embmMatrix3" };

	for (int i = 0; i < IDRV_MAT_MAXTEXTURES; ++i)
	{
		ss << "    // Stage " << i << std::endl;
		ss << "    if ((nlTextureActive & " << (1 << i) << ") != 0) {" << std::endl;

		// EMBM re-sample: if previous stage was EMBM, re-sample current texture with offset
		ss << "      vec4 t" << i << " = texel" << i << ";" << std::endl;
		ss << "      if (prevEMBM) {" << std::endl;
		if (i < IDRV_MAT_MAXTEXTURES)
		{
			ss << "        vec2 rtc = ((nlVertexFormat & NL_PP_TEXCOORD" << i << "_FLAG) != 0) ? texCoord" << i << ".st : texCoord0.st;" << std::endl;
		}
		else
		{
			ss << "        vec2 rtc = texCoord0.st;" << std::endl;
		}
		if (cube && i == 1)
			ss << "        t" << i << " = texture(sampler" << i << ", vec3(rtc + embmOfs, 0.0));" << std::endl;
		else
			ss << "        t" << i << " = texture(sampler" << i << ", rtc + embmOfs);" << std::endl;
		ss << "        prevEMBM = false;" << std::endl;
		ss << "      }" << std::endl;

		// Check for EMBM
		ss << "      int opRGB" << i << " = int(" << envNames[i] << " & 0xFu);" << std::endl;
		ss << "      if (opRGB" << i << " == 8) {" << std::endl; // EMBM
		ss << "        embmOfs = vec2(dot(" << embmNames[i] << ".xy, t" << i << ".rg), dot(" << embmNames[i] << ".zw, t" << i << ".rg));" << std::endl;
		ss << "        prevEMBM = true;" << std::endl;
		ss << "      } else {" << std::endl;
		ss << "        texEnvResult = applyTexEnv(" << envNames[i] << ", t" << i << ", texEnvResult, fragColor, " << cnstNames[i] << ");" << std::endl;
		ss << "      }" << std::endl;

		ss << "    } else if (" << i << " == 0) {" << std::endl;
		ss << "      texEnvResult = fragColor;" << std::endl;
		ss << "    }" << std::endl;
	}
	ss << "    fragColor = texEnvResult;" << std::endl;
	ss << std::endl;

	// Specular (2)
	ss << "  } else if (nlShader == 2) {" << std::endl;
	ss << "    if ((nlTextureActive & 1) != 0) {" << std::endl;
	ss << "      vec3 specop0 = texel0.rgb * fragColor.rgb;" << std::endl;
	ss << "      if ((nlTextureActive & 2) != 0)" << std::endl;
	ss << "        fragColor = vec4(texel1.rgb * texel0.a + specop0, fragColor.a);" << std::endl;
	ss << "      else" << std::endl;
	ss << "        fragColor = vec4(specop0, fragColor.a);" << std::endl;
	ss << "    } else if ((nlTextureActive & 2) != 0) {" << std::endl;
	ss << "      fragColor = vec4(texel1.rgb + fragColor.rgb, 1.0);" << std::endl;
	ss << "    }" << std::endl;
	ss << std::endl;

	// LightMap (3)
	ss << "  } else if (nlShader == 3) {" << std::endl;
	// Find last active stage and accumulate lightmaps
	ss << "    int lastStage = -1;" << std::endl;
	for (int i = MEGA_PP_MAX_SAMPLERS - 1; i >= 0; --i)
		ss << "    if (lastStage < 0 && (nlTextureActive & " << (1 << i) << ") != 0) lastStage = " << i << ";" << std::endl;
	ss << "    if (lastStage <= 0) {" << std::endl;
	ss << "      if (lastStage == 0) fragColor = texel0 * fragColor;" << std::endl;
	ss << "    } else {" << std::endl;
	ss << "      vec4 lmAccum = vec4(0.0);" << std::endl;
	for (int i = 0; i < MEGA_PP_MAX_SAMPLERS; ++i)
	{
		ss << "      if (" << i << " < lastStage && (nlTextureActive & " << (1 << i) << ") != 0)" << std::endl;
		ss << "        lmAccum += texel" << i << " * constant" << i << ";" << std::endl;
	}
	// Last stage is diffuse texture
	for (int i = 0; i < MEGA_PP_MAX_SAMPLERS; ++i)
	{
		ss << "      if (lastStage == " << i << ") {" << std::endl;
		ss << "        fragColor.rgb = texel" << i << ".rgb * (fragColor.rgb + lmAccum.rgb);" << std::endl;
		ss << "        fragColor.a = texel" << i << ".a;" << std::endl;
		ss << "      }" << std::endl;
	}
	ss << "    }" << std::endl;

	ss << "  }" << std::endl;
	ss << std::endl;

	// Add specular post-texture (matches legacy GL_COLOR_SUM / GL_SEPARATE_SPECULAR_COLOR)
	if (specular)
		ss << "  fragColor.rgb += specularColor.rgb;" << std::endl;
	ss << std::endl;

	// Alpha test
	ss << "  if (nlAlphaTest != 0 && fragColor.a <= alphaRef) discard;" << std::endl;
	ss << std::endl;

	// Fog
	if (fog)
		ss << "  fragColor = applyFog(fragColor);" << std::endl;

	ss << "}" << std::endl;

	result = ss.str();
}

} /* anonymous namespace */

bool CDriverGL3::initMegaPixelPrograms()
{
	for (int fog = 0; fog < 2; ++fog)
	{
		for (int cube = 0; cube < 2; ++cube)
		{
			for (int specular = 0; specular < 2; ++specular)
			{
				for (int cameraUBO = 0; cameraUBO < 2; ++cameraUBO)
				{
					for (int objectUBO = 0; objectUBO < 2; ++objectUBO)
					{
						for (int materialUBO = 0; materialUBO < 2; ++materialUBO)
						{
							// objectUBO implies cameraUBO
							if (objectUBO && !cameraUBO)
								continue;

							std::string result;
							megaPPGenerate(result, fog != 0, cube != 0, specular != 0, cameraUBO != 0, objectUBO != 0, materialUBO != 0);

							CPixelProgram *pp = new CPixelProgram();
							IProgram::CSource *src = new IProgram::CSource();
							src->Profile = IProgram::glsl330f;
							src->DisplayName = NLMISC::toString("Mega PP (fog=%d, cube=%d, spec=%d, cam=%d, obj=%d, mat=%d)", fog, cube, specular, cameraUBO, objectUBO, materialUBO);
							src->Features.UsesCameraUBO = (cameraUBO != 0);
							src->Features.UsesObjectUBO = (objectUBO != 0);
							src->Features.UsesMaterialUBO = (materialUBO != 0);
							src->setSource(result);
							pp->addSource(src);

							nldebug("GL3: Compile '%s'", src->DisplayName.c_str());

							if (!compilePixelProgram(pp))
							{
								nlwarning("GL3: Mega PP compilation failed (%s)", src->DisplayName.c_str());
								delete pp;
								return false;
							}

							m_MegaPP[fog][cube][specular][cameraUBO][objectUBO][materialUBO] = pp;
						}
					}
				}
			}
		}
	}
	return true;
}

bool CDriverGL3::setupMegaPixelProgram()
{
	if (m_UserPixelProgram) return true;

	nlassert(_CurrentMaterial);
	CMaterial &mat = *_CurrentMaterial;
	CMaterialDrvInfosGL3 *matDrv = static_cast<CMaterialDrvInfosGL3 *>((IMaterialDrvInfos *)(mat._MatDrvInfo));
	nlassert(matDrv);

	// Update PPBuiltin cached state. In the mega path, Touched is not consumed
	// (no per-material PP compilation), but the check functions update PPBuiltin
	// fields that uploadMaterialUBO() reads (Shader, TextureActive, TexEnvMode, Flags).
	matDrv->PPBuiltin.checkDriverStateTouched(this);
	matDrv->PPBuiltin.checkDriverMaterialStateTouched(this, mat);
	matDrv->PPBuiltin.checkMaterialStateTouched(mat);

	// Propagate material-UBO-relevant changes only (not fog/vertexFormat/specular changes).
	if (matDrv->PPBuiltin.MaterialUBOTouched)
	{
		matDrv->MaterialUBODirty = true;
		matDrv->PPBuiltin.MaterialUBOTouched = false;
	}

	int fog = m_VPBuiltinCurrent.Fog ? 1 : 0;
	// Cube variant: any cubemap in the material's sampler modes
	int cube = (matDrv->PPBuiltin.TexSamplerMode != 0) ? 1 : 0;
	int specular = m_VPSpecularOutput ? 1 : 0;
	int cameraUBO = m_ProgramUsesCameraUBO[VertexProgram] ? 1 : 0;
	int objectUBO = m_ProgramUsesObjectUBO[VertexProgram] ? 1 : 0;
	int materialUBO = m_ProgramUsesMaterialUBO[VertexProgram] ? 1 : 0;

	CPixelProgram *pp = m_MegaPP[fog][cube][specular][cameraUBO][objectUBO][materialUBO];
	nlassert(pp);

	if (!activePixelProgram(pp, true))
		return false;

	setupMegaPPUniforms();
	return true;
}

void CDriverGL3::setupMegaPPUniforms()
{
	IProgram *p = m_DriverPixelProgram;
	if (!p) return;
	IProgramDrvInfos *di = p->m_DrvInfo;
	if (!di) return;
	CProgramDrvInfosGL3 *drvInfo = static_cast<CProgramDrvInfosGL3 *>(di);
	GLuint progId = drvInfo->getProgramId();
	if (!progId) return;

	CMaterial &mat = *_CurrentMaterial;
	CMaterialDrvInfosGL3 *matDrv = static_cast<CMaterialDrvInfosGL3 *>((IMaterialDrvInfos *)(mat._MatDrvInfo));

	uint idx;

	// When material UBO is active, nlShader/nlTextureActive/nlTexEnvMode/nlAlphaTest are in UBO
	if (!m_ProgramUsesMaterialUBO[PixelProgram])
	{
		// Shader type
		CMaterial::TShader shader = matDrv->PPBuiltin.Shader;
		int shaderInt = 0;
		switch (shader)
		{
		case CMaterial::Normal:    shaderInt = 0; break;
		case CMaterial::UserColor: shaderInt = 1; break;
		case CMaterial::Specular:  shaderInt = 2; break;
		case CMaterial::LightMap:  shaderInt = 3; break;
		default:                   shaderInt = 0; break;
		}
		idx = p->getUniformIndex(CProgramIndex::NlShader);
		if (idx != ~0u)
			nglProgramUniform1i(progId, idx, shaderInt);

		// Texture active bitmask
		idx = p->getUniformIndex(CProgramIndex::NlTextureActive);
		if (idx != ~0u)
			nglProgramUniform1i(progId, idx, (sint32)matDrv->PPBuiltin.TextureActive);

		// TexEnv modes (packed uint32)
		for (int i = 0; i < IDRV_MAT_MAXTEXTURES; ++i)
		{
			idx = p->getUniformIndex((CProgramIndex::TName)(CProgramIndex::NlTexEnvMode0 + i));
			if (idx != ~0u)
				nglProgramUniform1ui(progId, idx, matDrv->PPBuiltin.TexEnvMode[i]);
		}

		// Alpha test
		idx = p->getUniformIndex(CProgramIndex::NlAlphaTest);
		if (idx != ~0u)
			nglProgramUniform1i(progId, idx, (matDrv->PPBuiltin.Flags & IDRV_MAT_ALPHA_TEST) ? 1 : 0);
	}

	// Fog mode and camera forward (skip when camera UBO provides them)
	if (!m_ProgramUsesCameraUBO[PixelProgram])
	{
		idx = p->getUniformIndex(CProgramIndex::NlFogMode);
		if (idx != ~0u)
			nglProgramUniform1i(progId, idx, (int)_FogMode);

		// Camera forward for world-space fog (second row of view matrix, NeL Y = forward)
		idx = p->getUniformIndex(CProgramIndex::CameraForward);
		if (idx != ~0u)
		{
			const float *v = _ViewMtx.get();
			nglProgramUniform3f(progId, idx, v[1], v[5], v[9]);
		}
	}

	// Vertex format (skip when object UBO provides it)
	if (!m_ProgramUsesObjectUBO[PixelProgram])
	{
		idx = p->getUniformIndex(CProgramIndex::NlVertexFormat);
		if (idx != ~0u)
			nglProgramUniform1i(progId, idx, (sint32)matDrv->PPBuiltin.VertexFormat);

		idx = p->getUniformIndex(CProgramIndex::NlWorldSpacePosition);
		if (idx != ~0u)
			nglProgramUniform1i(progId, idx, m_VPWorldSpacePositionOutput ? 1 : 0);
	}
}

} // NLDRIVERGL3
} // NL3D

/* end of file */
