// NeL - MMORPG Framework <https://wiki.ryzom.dev/>
// Copyright (C) 2025-2026  Jan BOON (Kaetemi) <jan.boon@kaetemi.be>
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

#include "std3d.h"

#include "nel/3d/gpu_skin_vp.h"
#include "nel/3d/program.h"
#include "nel/3d/uniform_buffer_format.h"
#include "nel/3d/uniform_buffer.h"

namespace NL3D {

// GLSL snippet for the VP insert.
// This defines the NlSkinning UBO and the nlPreTransform function.
// The UBO block declaration is handled separately via UniformBufferFormats;
// this source only contains the function body.
// Bones are stored as 3 row-vectors (vec4) per bone in the UBO:
//   bones[b+0] = row0 = (Xx, Xy, Xz, Tx)
//   bones[b+1] = row1 = (Yx, Yy, Yz, Ty)
//   bones[b+2] = row2 = (Zx, Zy, Zz, Tz)
// Skinning: skinnedPos = bone * vec4(pos, 1) via 3 dot products per row.
// Normal/tangent: use .xyz of each row (rotation only, no translation).
static const char *s_GPUSkinInsertGLSL =
	"void nlPreTransform(inout vec4 pos, inout vec3 norm, inout vec4 tc0, inout vec4 tangent)\n"
	"{\n"
	"    ivec4 boneIdx = ivec4(vpaletteSkin);\n"
	"    vec4 boneWgt = vweight;\n"
	"\n"
	"    // MRM geomorph (vertices above threshold are morphing out)\n"
	"    if (gl_VertexID >= morphThreshold) {\n"
	"        pos.xyz = mix(vtexCoord1.xyz, pos.xyz, morphAlpha);\n"
	"        norm = mix(vtexCoord2.xyz, norm, morphAlpha);\n"
	"        tc0.xy = mix(vtexCoord3.xy, tc0.xy, morphAlpha);\n"
	"        tangent.xyz = mix(vtexCoord4.xyz, tangent.xyz, morphAlpha);\n"
	"    }\n"
	"\n"
	"    // Weighted bone skinning (3 row-vectors per bone)\n"
	"    vec3 skinnedPos = vec3(0.0);\n"
	"    vec3 skinnedNorm = vec3(0.0);\n"
	"    vec3 skinnedTan = vec3(0.0);\n"
	"    vec4 p = vec4(pos.xyz, 1.0);\n"
	"    for (int i = 0; i < 4; i++) {\n"
	"        float w = boneWgt[i];\n"
	"        if (w <= 0.0) continue;\n"
	"        int b = boneIdx[i] * 3;\n"
	"        vec4 row0 = bones[b];\n"
	"        vec4 row1 = bones[b+1];\n"
	"        vec4 row2 = bones[b+2];\n"
	"        skinnedPos += w * vec3(dot(row0, p), dot(row1, p), dot(row2, p));\n"
	"        skinnedNorm += w * vec3(dot(row0.xyz, norm), dot(row1.xyz, norm), dot(row2.xyz, norm));\n"
	"        skinnedTan += w * vec3(dot(row0.xyz, tangent.xyz), dot(row1.xyz, tangent.xyz), dot(row2.xyz, tangent.xyz));\n"
	"    }\n"
	"\n"
	"    pos = vec4(skinnedPos, 1.0);\n"
	"    norm = normalize(skinnedNorm);\n"
	"    tangent = vec4(normalize(skinnedTan), tangent.w);\n"
	"}\n";

static CVertexProgram *s_GPUSkinInsertVP = NULL;

CVertexProgram *getGPUSkinInsertVP()
{
	if (s_GPUSkinInsertVP)
		return s_GPUSkinInsertVP;

	CVertexProgram *vp = new CVertexProgram();
	IProgram::CSource *src = new IProgram::CSource();
	src->Profile = IProgram::glsl3vi;
	src->DisplayName = "GPU Skinning Insert";
	src->setSource(s_GPUSkinInsertGLSL);

	// Attach NlSkinning UBO format at user VP UBO binding
	CUniformBufferFormat *fmt = new CUniformBufferFormat();
	fmt->Name = "NlSkinning";
	fmt->push("morphThreshold", CUniformBufferFormat::SInt, 1);
	fmt->push("morphAlpha", CUniformBufferFormat::Float, 1);
	fmt->push("bones", CUniformBufferFormat::FloatVec4, NL3D_GPU_SKIN_MAX_BONES * 3);

	src->UniformBufferFormats[UBBindingVertexProgram] = fmt;
	vp->addSource(src);

	s_GPUSkinInsertVP = vp;
	return vp;
}

} // NL3D

/* end of file */
