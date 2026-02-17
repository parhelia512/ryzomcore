// NeL - MMORPG Framework <http://dev.ryzom.com/projects/nel/>
// Copyright (C) 2014-2015  Jan BOON (Kaetemi) <jan.boon@kaetemi.be>
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

#include <sstream>

#include <nel/misc/debug.h>

namespace NL3D {
namespace NLDRIVERGL3 {

const char *GLSLBuiltinHeader =
	// Light table UBO: shared across all objects, uploaded once when lights change.
	// User VPs can reference nlLights[] directly when UsesLightTableUBO is set.
	// Binding point is set from the CPU via glUniformBlockBinding in setupInitialUniforms.
	"struct NlLightInfo {\n"
	"    vec3  dirOrPos;\n"
	"    int   mode;\n"        // 0=directional, 1=point, 2=spot
	"    vec4  diffuse;\n"
	"    vec4  specular;\n"
	"    float constAttn;\n"
	"    float linAttn;\n"
	"    float quadAttn;\n"
	"    float spotExp;\n"
	"    vec3  spotDir;\n"
	"    float spotCutoff;\n"  // cos(cutoff angle)
	"};\n"
	"layout(std140) uniform NlLightTable {\n"
	"    NlLightInfo nlLights[128];\n"
	"};\n";

// Draft UBO infrastructure arrays — indices match the _BINDING defines
static const char *s_UniformBufferBindDefine[] = {
	"0", // NL_BUILTIN_CAMERA_BINDING
	"1", // NL_BUILTIN_LIGHT_TABLE_BINDING
	"2", // NL_BUILTIN_MODEL_BINDING (draft)
	"3", // NL_BUILTIN_MATERIAL_BINDING (draft)
	"4", // NL_USER_ENV_BINDING (draft)
	"5", // NL_USER_VERTEX_PROGRAM_BINDING (draft)
	"6", // NL_USER_GEOMETRY_PROGRAM_BINDING (draft)
	"7", // NL_USER_PIXEL_PROGRAM_BINDING (draft)
	"8", // NL_USER_MATERIAL_BINDING (draft)
};

static const char *s_UniformBufferName[] = {
	"BuiltinCamera",
	"NlLightTable",
	"BuiltinModel",       // draft
	"BuiltinMaterial",     // draft
	"UserEnv",             // draft
	"UserLocal",           // draft
	"UserLocal",           // draft
	"UserLocal",           // draft
	"UserMaterial",        // draft
};

static const char *s_TypeKeyword[] = {
	"float", // float
	"vec2", // CVector2D
	"vec3",
	"vec4", // CVector
	"int", // sint32
	"ivec2",
	"ivec3",
	"ivec3",
	"unsigned int", // uint32
	"uvec2",
	"uvec3",
	"uvec4",
	"bool",
	"bvec2",
	"bvec3",
	"bvec4",
	"mat2",
	"mat3",
	"mat4", // CMatrix
	"mat2x3",
	"mat2x4",
	"mat3x2",
	"mat3x4",
	"mat4x2",
	"mat4x3",
};

void generateUniformBufferGLSL(std::stringstream &ss, const CUniformBufferFormat &ubf, sint binding)
{
	// Emit struct definitions
	for (sint i = 0; i < ubf.structCount(); ++i)
	{
		ss << "struct " << ubf.getStructName(i) << "\n";
		ss << "{\n";
		const CUniformBufferFormat &sf = ubf.getStructFormat(i);
		for (sint j = 0; j < sf.count(); ++j)
		{
			const CUniformBufferFormat::CEntry &field = sf.get(j);
			ss << "\t" << s_TypeKeyword[field.Type] << " " << NLMISC::CStringMapper::unmap(field.Name);
			if (field.Count != 1)
				ss << "[" << field.Count << "]";
			ss << ";\n";
		}
		ss << "};\n";
	}

	ss << "layout(std140, binding = " << s_UniformBufferBindDefine[binding] << ") uniform " << s_UniformBufferName[binding] << "\n";
	ss << "{\n";
	for (sint i = 0; i < ubf.count(); ++i)
	{
		const CUniformBufferFormat::CEntry &entry = ubf.get(i);
		if (entry.StructIndex >= 0)
			ss << "\t" << ubf.getStructName(entry.StructIndex);
		else
			ss << "\t" << s_TypeKeyword[entry.Type];
		ss << " " << NLMISC::CStringMapper::unmap(entry.Name);
		if (entry.Count != 1)
			ss << "[" << entry.Count << "]";
		ss << ";\n";
	}
	ss << "}\n";
}

} // NLDRIVERGL3
} // NL3D

/* end of file */
