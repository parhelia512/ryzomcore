// NeL - MMORPG Framework <http://dev.ryzom.com/projects/nel/>
// Copyright (C) 2010  Winch Gate Property Limited
//
// This source file has been modified by the following contributors:
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

#include "driver_opengl.h"
#include "driver_opengl_program.h"
#include "driver_opengl_vertex_buffer.h"
#include "driver_opengl_uniform_buffer.h"

namespace NL3D {
namespace NLDRIVERGL3 {

// Insert builtin UBO headers after leading # preprocessor lines (#version, #extension, etc.)
static std::string insertBuiltinHeaders(const char *source, bool lightTable, bool camera, bool object, bool material,
	const std::map<sint, NLMISC::CSmartPtr<CUniformBufferFormat> > &userUBOs)
{
	const char *p = source;

	// If source starts with # or //, scan past all # and // lines
	if (*p == '#' || *p == '/' || *p == '\n')
	{
		for (;;)
		{
			// Find end of current line
			while (*p && *p != '\n')
				++p;
			if (*p == '\n')
				++p;
			// Stop if next line doesn't start with # or is empty
			if (*p != '#' && *p != '/' && *p != '\n')
				break;
		}
	}

	std::string result;
	result.append(source, p - source);
	if (camera)
		result.append(GLSLCameraHeader);
	if (lightTable)
		result.append(GLSLLightTableHeader);
	if (object)
		result.append(GLSLObjectHeader);
	if (material)
		result.append(GLSLMaterialHeader);

	// User UBO declarations — map key is TUBBinding enum (0=VP, 1=PP),
	// translate to GL binding points for fallback block name generation
	if (!userUBOs.empty())
	{
		static const sint s_UBBindingToGLSL[] = {
			NL_USER_VERTEX_PROGRAM_BINDING,  // UBBindingVertexProgram
			NL_USER_PIXEL_PROGRAM_BINDING,   // UBBindingPixelProgram
		};
		std::stringstream ss;
		for (std::map<sint, NLMISC::CSmartPtr<CUniformBufferFormat> >::const_iterator it = userUBOs.begin(); it != userUBOs.end(); ++it)
		{
			nlassert(it->first >= 0 && it->first < UBBindingCount);
			generateUniformBufferGLSL(ss, *it->second, s_UBBindingToGLSL[it->first]);
		}
		result.append(ss.str());
	}

	result.append(p);
	return result;
}

const uint16 g_VertexFlags[CVertexBuffer::NumValue] = 
{
	CVertexBuffer::PositionFlag,
	CVertexBuffer::WeightFlag,
	CVertexBuffer::NormalFlag,
	CVertexBuffer::PrimaryColorFlag,
	CVertexBuffer::SecondaryColorFlag,
	CVertexBuffer::FogFlag,
	CVertexBuffer::PaletteSkinFlag,
	0,
	CVertexBuffer::TexCoord0Flag,
	CVertexBuffer::TexCoord1Flag,
	CVertexBuffer::TexCoord2Flag,
	CVertexBuffer::TexCoord3Flag,
	CVertexBuffer::TexCoord4Flag,
	CVertexBuffer::TexCoord5Flag,
	CVertexBuffer::TexCoord6Flag,
	CVertexBuffer::TexCoord7Flag
};

const char *g_AttribNames[CVertexBuffer::NumValue] =
{
	"position",
	"weight",
	"normal",
	"primaryColor",
	"secondaryColor",
	"fog",
	"paletteSkin",
	"none",
	"texCoord0",
	"texCoord1",
	"texCoord2",
	"texCoord3",
	"texCoord4",
	"texCoord5",
	"texCoord6",
	"texCoord7"
};

const char *g_TexelNames[IDRV_PROGRAM_MAXSAMPLERS] =
{
	"texel0",
	"texel1",
	"texel2",
	"texel3",
	"texel4",
	"texel5",
	"texel6",
	"texel7",
	"texel8",
	"texel9",
	"texel10",
	"texel11",
	"texel12",
	"texel13",
	"texel14",
	"texel15",
	"texel16",
	"texel17",
	"texel18",
	"texel19",
	"texel20",
	"texel21",
	"texel22",
	"texel23",
	"texel24",
	"texel25",
	"texel26",
	"texel27",
	"texel28",
	"texel29",
	"texel30",
	"texel31",
};

const char *g_ConstantNames[IDRV_PROGRAM_MAXSAMPLERS] =
{
	"constant0",
	"constant1",
	"constant2",
	"constant3",
	"constant4",
	"constant5",
	"constant6",
	"constant7",
	"constant8",
	"constant9",
	"constant10",
	"constant11",
	"constant12",
	"constant13",
	"constant14",
	"constant15",
	"constant16",
	"constant17",
	"constant18",
	"constant19",
	"constant20",
	"constant21",
	"constant22",
	"constant23",
	"constant24",
	"constant25",
	"constant26",
	"constant27",
	"constant28",
	"constant29",
	"constant30",
	"constant31",
};

bool CDriverGL3::supportVertexProgram(CVertexProgram::TProfile profile) const
{
	return (profile == IProgram::glsl330v);
}

bool CDriverGL3::compileVertexProgram(CVertexProgram *program)
{
	if (program->m_DrvInfo != NULL)
		return true;

	if (program->m_CompileFailed)
		return false;

	IProgram::CSource *src = NULL;
	for (int i = 0; i < program->getSourceNb(); i++)
	{
		src = program->getSource(i);
		if (src->Profile == IProgram::glsl330v)
			break;

		src = NULL;
	}
	if (src == NULL)
	{
		program->m_CompileFailed = true;
		return false;
	}

	// Object UBO implies light table UBO and camera UBO — write back to features
	if (src->Features.UsesObjectUBO)
	{
		src->Features.UsesLightTableUBO = true;
		src->Features.UsesCameraUBO = true;
	}

	std::string fullSource;
	const char *s;
	bool hasBuiltinUBO = src->Features.UsesLightTableUBO || src->Features.UsesCameraUBO
	                   || src->Features.UsesObjectUBO || src->Features.UsesMaterialUBO;
	bool hasUserUBO = !src->UniformBufferFormats.empty();
	if (hasBuiltinUBO || hasUserUBO)
	{
		fullSource = insertBuiltinHeaders(src->SourcePtr,
			src->Features.UsesLightTableUBO, src->Features.UsesCameraUBO,
			src->Features.UsesObjectUBO, src->Features.UsesMaterialUBO,
			src->UniformBufferFormats);
		s = fullSource.c_str();
	}
	else
	{
		s = src->SourcePtr;
	}
	unsigned int id = nglCreateShaderProgramv(GL_VERTEX_SHADER, 1, &s);

	if (id == 0)
	{
		program->m_CompileFailed = true;
		return false;
	}

	GLint ok;
	nglGetProgramiv(id, GL_LINK_STATUS, &ok);
	if (ok == 0)
	{
		char errorLog[ 1024 ];
		nglGetProgramInfoLog(id, 1024, NULL, errorLog);
		nlwarning("GL3: VP compile failed: %s", errorLog);
		std::vector<std::string> lines;
		NLMISC::explode(std::string(s), std::string("\n"), lines);
		for (std::vector<std::string>::size_type i = 0; i < lines.size(); ++i)
		{
			nldebug("GL3: %i: %s", i, lines[i].c_str());
		}
		program->m_CompileFailed = true;
#if !FINAL_VERSION
		nlerror("GL3: Vertex program compilation failed");
#endif
		return false;
	}

	ItGPUPrgDrvInfoPtrList it = _GPUPrgDrvInfos.insert(_GPUPrgDrvInfos.end(),(NL3D::IProgramDrvInfos*)NULL);
	CProgramDrvInfosGL3 *drvInfo = new CProgramDrvInfosGL3(this, it);
	*it = drvInfo;
	program->m_DrvInfo = drvInfo;
	drvInfo->setProgramId(id);

	program->buildInfo(src);

	setupInitialUniforms(program);

	return true;
}

bool CDriverGL3::activeVertexProgram(CVertexProgram *program)
{
	return activeVertexProgram(program, false);
}

bool CDriverGL3::activeVertexProgram(CVertexProgram *program, bool driver)
{
	if (driver) nlassert(m_UserVertexProgram == NULL);

	if (m_DriverVertexProgram == program)
		return true;

	if (program == NULL)
	{
		nglUseProgramStages(ppoId, GL_VERTEX_SHADER_BIT, 0);
		m_UserVertexProgram = NULL;
		m_DriverVertexProgram = NULL;
		return true;
	}

	IProgramDrvInfos *di = program->m_DrvInfo;
	if (di == NULL)
	{
		if (!compileVertexProgram(program))
		{
			m_UserVertexProgram = NULL;
			m_DriverVertexProgram = NULL;
			return false;
		}
		di = program->m_DrvInfo;
	}
	CProgramDrvInfosGL3 *drvInfo = static_cast<CProgramDrvInfosGL3 *>(di);

	nglUseProgramStages(ppoId, GL_VERTEX_SHADER_BIT, drvInfo->getProgramId());

	if (!driver) m_UserVertexProgram = program;
	m_DriverVertexProgram = program;
	return true;
}

bool CDriverGL3::supportPixelProgram(IProgram::TProfile profile) const
{
	if (profile == IProgram::glsl330f)
		return true;
	else
		return false;
}

bool CDriverGL3::compilePixelProgram(CPixelProgram *program)
{
	if (program->m_DrvInfo != NULL)
		return true;

	if (program->m_CompileFailed)
		return false;

	IProgram::CSource *src = NULL;

	for (int i = 0; i < program->getSourceNb(); i++)
	{
		src = program->getSource(i);
		if (src->Profile == IProgram::glsl330f)
			break;

		src = NULL;
	}

	if (src == NULL)
	{
		program->m_CompileFailed = true;
		return false;
	}

	// Object UBO implies light table UBO and camera UBO — write back to features
	if (src->Features.UsesObjectUBO)
	{
		src->Features.UsesLightTableUBO = true;
		src->Features.UsesCameraUBO = true;
	}

	std::string fullSource;
	const char *s;
	bool hasBuiltinUBO = src->Features.UsesLightTableUBO || src->Features.UsesCameraUBO
	                   || src->Features.UsesObjectUBO || src->Features.UsesMaterialUBO;
	bool hasUserUBO = !src->UniformBufferFormats.empty();
	if (hasBuiltinUBO || hasUserUBO)
	{
		fullSource = insertBuiltinHeaders(src->SourcePtr,
			src->Features.UsesLightTableUBO, src->Features.UsesCameraUBO,
			src->Features.UsesObjectUBO, src->Features.UsesMaterialUBO,
			src->UniformBufferFormats);
		s = fullSource.c_str();
	}
	else
	{
		s = src->SourcePtr;
	}
	unsigned int id = nglCreateShaderProgramv(GL_FRAGMENT_SHADER, 1, &s);
	if (id == 0)
	{
		program->m_CompileFailed = true;
		return false;
	}

	GLint ok;
	nglGetProgramiv(id, GL_LINK_STATUS, &ok);
	if (ok == 0)
	{
		char errorLog[ 1024 ];
		nglGetProgramInfoLog(id, 1024, NULL, errorLog);
		nlwarning("GL3: PP compile failed: %s", errorLog);
		std::vector<std::string> lines;
		NLMISC::explode(std::string(s), std::string("\n"), lines);
		for (std::vector<std::string>::size_type i = 0; i < lines.size(); ++i)
		{
			nldebug("GL3: %i: %s", i, lines[i].c_str());
		}
		program->m_CompileFailed = true;
#if !FINAL_VERSION
		nlerror("GL3: Pixel program compilation failed");
#endif
		return false;
	}

	ItGPUPrgDrvInfoPtrList it = _GPUPrgDrvInfos.insert(_GPUPrgDrvInfos.end(), (NL3D::IProgramDrvInfos*)NULL);
	CProgramDrvInfosGL3 *drvInfo = new CProgramDrvInfosGL3(this, it);
	*it = drvInfo;
	drvInfo->setProgramId(id);
	program->m_DrvInfo = drvInfo;

	program->buildInfo(src);

	setupInitialUniforms(program);

	return true;
}

bool CDriverGL3::activePixelProgram(CPixelProgram *program)
{
	return activePixelProgram(program, false);
}

bool CDriverGL3::activePixelProgram(CPixelProgram *program, bool driver)
{
	if (driver) nlassert(m_UserPixelProgram == NULL);

	if (m_DriverPixelProgram == program)
		return true;

	if (program == NULL)
	{
		nglUseProgramStages(ppoId, GL_FRAGMENT_SHADER_BIT, 0);
		m_UserPixelProgram = NULL;
		m_DriverPixelProgram = NULL;
		m_ProgramUsesLightTableUBO[PixelProgram] = false;
		m_ProgramUsesCameraUBO[PixelProgram] = false;
		m_ProgramUsesObjectUBO[PixelProgram] = false;
		m_ProgramUsesMaterialUBO[PixelProgram] = false;
		return true;
	}

	IProgramDrvInfos *di = program->m_DrvInfo;
	if (di == NULL)
	{
		if (!compilePixelProgram(program))
		{
			m_UserPixelProgram = NULL;
			m_DriverPixelProgram = NULL;
			m_ProgramUsesLightTableUBO[PixelProgram] = false;
			m_ProgramUsesCameraUBO[PixelProgram] = false;
			m_ProgramUsesObjectUBO[PixelProgram] = false;
			m_ProgramUsesMaterialUBO[PixelProgram] = false;
			return false;
		}
		di = program->m_DrvInfo;
	}
	CProgramDrvInfosGL3 *drvInfo = static_cast<CProgramDrvInfosGL3 *>(di);

	nglUseProgramStages(ppoId, GL_FRAGMENT_SHADER_BIT, drvInfo->getProgramId());

	if (!driver) m_UserPixelProgram = program;
	m_DriverPixelProgram = program;

	// Set per-program UBO flags from the activated PP's features
	m_ProgramUsesLightTableUBO[PixelProgram] = program->features().UsesLightTableUBO;
	m_ProgramUsesCameraUBO[PixelProgram] = program->features().UsesCameraUBO;
	m_ProgramUsesObjectUBO[PixelProgram] = program->features().UsesObjectUBO;
	m_ProgramUsesMaterialUBO[PixelProgram] = program->features().UsesMaterialUBO;
	return true;
}


uint32 CDriverGL3::getProgramId(TProgram program) const
{
	IProgramDrvInfos *di;
	switch(program)
	{
	case IDriver::VertexProgram:
		if (m_DriverVertexProgram)
			di = m_DriverVertexProgram->m_DrvInfo;
		else
			di = NULL;
		break;
	case IDriver::PixelProgram:
		if (m_DriverPixelProgram)
			di = m_DriverPixelProgram->m_DrvInfo;
		else
			di = NULL;
		break;
	default:
		di = NULL;
		break;
	}
	
	if (di == NULL)
		return 0;
	
	CProgramDrvInfosGL3 *drvInfo = static_cast<CProgramDrvInfosGL3 *>(di);
	return drvInfo->getProgramId();
}

IProgram* CDriverGL3::getProgram(TProgram program) const
{
	switch(program)
	{
	case IDriver::VertexProgram:
		return m_DriverVertexProgram;
	case IDriver::PixelProgram:
		return m_DriverPixelProgram;
	case IDriver::GeometryProgram:
		return m_DriverGeometryProgram;
	}

	return NULL;
}

int CDriverGL3::getUniformLocation(TProgram program, const char *name)
{
	uint32 id = getProgramId(program);
	return nglGetUniformLocation(id, name);
}

void CDriverGL3::setUniform1f(TProgram program, uint index, float f0)
{
	uint32 id = getProgramId(program);
	nglProgramUniform1f(id, index, f0);
}

void CDriverGL3::setUniform2f(TProgram program, uint index, float f0, float f1)
{
	uint32 id = getProgramId(program);
	nglProgramUniform2f(id, index, f0, f1);
}

void CDriverGL3::setUniform3f(TProgram program, uint index, float f0, float f1, float f2)
{
	uint32 id = getProgramId(program);
	nglProgramUniform3f(id, index, f0, f1, f2);
}

void CDriverGL3::setUniform4f(TProgram program, uint index, float f0, float f1, float f2, float f3)
{
	uint32 id = getProgramId(program);
	nglProgramUniform4f(id, index, f0, f1, f2, f3);
}

void CDriverGL3::setUniform1i(TProgram program, uint index, sint32 i0)
{
	uint32 id = getProgramId(program);
	nglProgramUniform1i(id, index, i0);
}

void CDriverGL3::setUniform2i(TProgram program, uint index, sint32 i0, sint32 i1)
{
	uint32 id = getProgramId(program);
	nglProgramUniform2i(id, index, i0, i1);
}

void CDriverGL3::setUniform3i(TProgram program, uint index, sint32 i0, sint32 i1, sint32 i2)
{
	uint32 id = getProgramId(program);
	nglProgramUniform3i(id, index, i0, i1, i2);
}

void CDriverGL3::setUniform4i(TProgram program, uint index, sint32 i0, sint32 i1, sint32 i2, sint32 i3)
{
	uint32 id = getProgramId(program);
	nglProgramUniform4i(id, index, i0, i1, i2, i3);
}

void CDriverGL3::setUniform1ui(TProgram program, uint index, uint32 ui0)
{
	uint32 id = getProgramId(program);
	nglProgramUniform1ui(id, index, ui0);
}

void CDriverGL3::setUniform2ui(TProgram program, uint index, uint32 ui0, uint32 ui1)
{
	uint32 id = getProgramId(program);
	nglProgramUniform2ui(id, index, ui0, ui1);
}

void CDriverGL3::setUniform3ui(TProgram program, uint index, uint32 ui0, uint32 ui1, uint32 ui2)
{
	uint32 id = getProgramId(program);
	nglProgramUniform3ui(id, index, ui0, ui1, ui2);
}

void CDriverGL3::setUniform4ui(TProgram program, uint index, uint32 ui0, uint32 ui1, uint32 ui2, uint32 ui3)
{
	uint32 id = getProgramId(program);
	nglProgramUniform4ui(id, index, ui0, ui1, ui2, ui3);
}

void CDriverGL3::setUniform3f(TProgram program, uint index, const CVector &v)
{
	uint32 id = getProgramId(program);
	nglProgramUniform3f(id, index, v.x, v.y, v.z);
}

void CDriverGL3::setUniform4f(TProgram program, uint index, const CVector &v, float f3)
{
	uint32 id = getProgramId(program);
	nglProgramUniform4f(id, index, v.x, v.y, v.z, f3);
}

void CDriverGL3::setUniform4f(TProgram program, uint index, const NLMISC::CRGBAF& rgba)
{
	uint32 id = getProgramId(program);
	nglProgramUniform4f(id, index, rgba.R, rgba.G, rgba.B, rgba.A);
}

void CDriverGL3::setUniform3x3f(TProgram program, uint index, const float *src)
{
	uint32 id = getProgramId(program);
	nglProgramUniformMatrix3fv(id, index, 1, false, src);
}

void CDriverGL3::setUniform4x4f(TProgram program, uint index, const CMatrix &m)
{
	uint32 id = getProgramId(program);
	nglProgramUniformMatrix4fv(id, index, 1, false, m.get());
}

void CDriverGL3::setUniform4x4f(TProgram program, uint index, const float *src)
{
	uint32 id = getProgramId(program);
	nglProgramUniformMatrix4fv(id, index, 1, false, src);
}

void CDriverGL3::setUniform4fv(TProgram program, uint index, size_t num, const float *src)
{
	uint32 id = getProgramId(program);
	nglProgramUniform4fv(id, index, num, src);
}

void CDriverGL3::setUniform4iv(TProgram program, uint index, size_t num, const sint32 *src)
{
	uint32 id = getProgramId(program);
	nglProgramUniform4iv(id, index, num, src);
}

void CDriverGL3::setUniform4uiv(TProgram program, uint index, size_t num, const uint32 *src)
{
	uint32 id = getProgramId(program);
	nglProgramUniform4uiv(id, index, num, src);
}

void CDriverGL3::setUniformMatrix(TProgram program, uint index, TMatrix matrix, TTransform transform)
{
	uint32 id = getProgramId(program);
	CMatrix mat;
	

	switch(matrix)
	{
	case IDriver::ModelView:
		mat = _ModelViewMatrix;
		break;
	case IDriver::Projection:
		mat = _GLProjMat * _ChangeBasis;
		break;
	case IDriver::ModelViewProjection:
		mat = _GLProjMat * _ChangeBasis * _ModelViewMatrix;
		break;
	}

	switch(transform)
	{
	case IDriver::Inverse:
		mat.invert();
		break;
	case IDriver::Transpose:
		mat.transpose();
		break;
	case IDriver::InverseTranspose:
		mat.transpose();
		mat.invert();
		break;
	}


	nglProgramUniformMatrix4fv(id, index, 1, false, mat.get());
}

void CDriverGL3::setUniformFog(TProgram program, uint index)
{
	uint32 id = getProgramId(program);
	const float *v = _ModelViewMatrix.get();
	nglProgramUniform4f(id, index, -v[ 2 ], -v[ 6 ], -v[ 10 ], -v[ 4 ]);
}

/*
void CDriverGL3::generateShaderDesc(CShaderDesc &desc, CMaterial &mat)
{
	desc.setShaderType(mat.getShader());
	uint16 vbFlags = _CurrentVertexBufferGL->VB->getVertexFormat();
	for (sint i = 0; i < IDRV_MAT_MAXTEXTURES; ++i)
	{
		if (m_VPBuiltinCurrent.TexGenMode[i] >= 0)
		{
			//nldebug("texgen hack for keeping pp simpler, stage %i, tex %s valid", i, mat.getTexture(i) ? "IS" : "NOT");
			vbFlags |= g_VertexFlags[TexCoord0 + i];
		}
	}
	desc.setVBFlags(vbFlags);
	
	if (mat.getShader() == CMaterial::LightMap)
		desc.setNLightMaps(mat._LightMaps.size());
	
	//int i = 0;

	if (mat.getShader() == CMaterial::Normal
		|| mat.getShader() == CMaterial::UserColor
		|| mat.getShader() == CMaterial::Specular)
	{
		bool useTextures = false;

		int maxTextures = std::min(int(IDRV_MAT_MAXTEXTURES), int(IDRV_MAT_MAXTEXTURES));

		if (mat.getShader() == CMaterial::Normal)
		{
			for (int i = 0; i < maxTextures; i++)
			{
				desc.setTexEnvMode(i, mat.getTexEnvMode(i));
			} // todo specular env mode..
		}

		for (int i = 0; i < maxTextures; i++)
		{
			// GL3 TEX COORD
			if (mat.getTexture(i) != NULL 
				&& (desc.hasVBFlags(g_VertexFlags[TexCoord0]) || desc.hasVBFlags(g_VertexFlags[TexCoord0 + i]))
				)
			{
				//nldebug("use stage %i", i);

				desc.setUseTexStage(i, true);
				useTextures = true;

				desc.textureSamplerMode[i] = mat.getTexture(i)->isTextureCube() ? SamplerCube : Sampler2D; // Driver state
			}
			else
			{
				/*nldebug("stage fail %i, tex %s, tc0 %s, tci, %s", i, mat.getTexture(i) ? "VALID" : "NO", 
					desc.hasVBFlags(g_VertexFlags[TexCoord0]) ? "YES" : "NO",
					desc.hasVBFlags(g_VertexFlags[TexCoord0 + i]) ? "YES" : "NO");* /
			}
		}

		if (!useTextures)
		{
			desc.setNoTextures(true);
		}
	}

	if (mat.getAlphaTest())
	{
		desc.setAlphaTest(true);
		desc.setAlphaTestThreshold(mat.getAlphaTestThreshold());
	}

	if (m_VPBuiltinCurrent.Fog) // Driver state
	{
		desc.setFog(true);
		desc.setFogMode(CShaderDesc::Linear);
	}

	//bool enableLights = false;
	for (int i = 0; i < MaxLight; i++)
	{
		if (!_LightEnable[i])
		{
			desc.setLight(i, CShaderDesc::Nolight);
			continue;
		}
		//if (!_LightEnable[ i ])
		//	continue;

		//enableLights = true;
		
		switch(_LightMode[ i ])
		{
		case CLight::DirectionalLight:
			desc.setLight(i, CShaderDesc::Directional);
			break;
		
		case CLight::PointLight:
			desc.setLight(i, CShaderDesc::Point);
			break;
		
		case CLight::SpotLight:
			desc.setLight(i, CShaderDesc::Spot);
			break;
		}
	
	}

	desc.setLighting(/*enableLights && mat.isLighted() &&* / m_VPBuiltinCurrent.Lighting);			
}*/

bool CDriverGL3::setupBuiltinPrograms()
{
	return setupBuiltinVertexProgram()
		&& setupBuiltinPixelProgram()
		&& setupUniforms();
}

bool CDriverGL3::setupBuiltinVertexProgram()
{
	touchVertexFormatVP(); // Always update — PP builtin depends on vertex format

	if (m_UseMegaShaders) return setupMegaVertexProgram();

	if (m_UserVertexProgram)
	{
		m_VPSpecularOutput = m_UserVertexProgram->features().OutputsSpecularColor;
		m_VPNormalOutput = m_UserVertexProgram->features().OutputsWorldSpaceNormal;
		m_ProgramUsesLightTableUBO[VertexProgram] = m_UserVertexProgram->features().UsesLightTableUBO;
		m_ProgramUsesCameraUBO[VertexProgram] = m_UserVertexProgram->features().UsesCameraUBO;
		m_ProgramUsesObjectUBO[VertexProgram] = m_UserVertexProgram->features().UsesObjectUBO;
		m_ProgramUsesMaterialUBO[VertexProgram] = m_UserVertexProgram->features().UsesMaterialUBO;
		// Object UBO implies table and camera UBO
		if (m_ProgramUsesObjectUBO[VertexProgram])
		{
			m_ProgramUsesLightTableUBO[VertexProgram] = true;
			m_ProgramUsesCameraUBO[VertexProgram] = true;
		}
		return true;
	}

	// Check if PP needs world-space normal varying
	bool needNormal = false;
	if (m_UserPixelProgram)
		needNormal = m_UserPixelProgram->features().InputsWorldSpaceNormal;
	setWorldSpaceNormalVP(needNormal);

	if (m_VPBuiltinTouched)
	{
		generateBuiltinVertexProgram();
		nlassert(m_VPBuiltinCurrent.VertexProgram);
		m_VPBuiltinTouched = false;
	}

	m_VPSpecularOutput = m_VPBuiltinCurrent.Lighting
		|| (m_VPBuiltinCurrent.VertexFormat & g_VertexFlags[SecondaryColor]);
	m_VPNormalOutput = m_VPBuiltinCurrent.WorldSpaceNormal
		&& (m_VPBuiltinCurrent.VertexFormat & g_VertexFlags[Normal]);
	m_ProgramUsesLightTableUBO[VertexProgram] = false; // Builtin non-mega VP does not use UBOs
	m_ProgramUsesCameraUBO[VertexProgram] = false;
	m_ProgramUsesObjectUBO[VertexProgram] = false;
	m_ProgramUsesMaterialUBO[VertexProgram] = false;

	if (!activeVertexProgram(m_VPBuiltinCurrent.VertexProgram, true))
		return false;

	// GL3 TODO: Here we set the uniforms of the vertex program!

	return true;
}

bool CDriverGL3::setupBuiltinPixelProgram()
{
	if (m_UseMegaShaders) return setupMegaPixelProgram();

	if (m_UserPixelProgram) return true;

	nlassert(_CurrentMaterial);
	CMaterial &mat = *_CurrentMaterial;
	CMaterialDrvInfosGL3 *matDrv = static_cast<CMaterialDrvInfosGL3 *>((IMaterialDrvInfos *)(mat._MatDrvInfo));
	nlassert(matDrv);

	matDrv->PPBuiltin.checkDriverStateTouched(this);
	matDrv->PPBuiltin.checkDriverMaterialStateTouched(this, mat);
	matDrv->PPBuiltin.checkMaterialStateTouched(mat);

	// Propagate material-UBO-relevant PP state changes (Shader, Flags, TextureActive,
	// TexEnvMode) to MaterialUBODirty. Driver-only state changes (VertexFormat, Fog,
	// FogMode, SpecularSeparate) are excluded to avoid unnecessary UBO re-uploads.
	if (matDrv->PPBuiltin.MaterialUBOTouched)
		matDrv->MaterialUBODirty = true;

	if (matDrv->PPBuiltin.Touched)
	{
		generateBuiltinPixelProgram(mat);
		nlassert(matDrv->PPBuiltin.PixelProgram);
		matDrv->PPBuiltin.Touched = false;
		matDrv->PPBuiltin.MaterialUBOTouched = false;
	}

	if (!activePixelProgram(matDrv->PPBuiltin.PixelProgram, true))
		return false;

	// GL3 TODO: Here we set the uniforms of the vertex program!

	return true;
}

bool CDriverGL3::setupUniforms()
{
	// Upload UBOs based on union of all active programs' needs
	if (m_ProgramUsesObjectUBO[VertexProgram] || m_ProgramUsesObjectUBO[PixelProgram])
		uploadObjectUBO();
	if (m_ProgramUsesMaterialUBO[VertexProgram] || m_ProgramUsesMaterialUBO[PixelProgram])
		uploadMaterialUBO();
	if (m_ProgramUsesCameraUBO[VertexProgram] || m_ProgramUsesCameraUBO[PixelProgram])
		uploadCameraUBO();
	if (m_ProgramUsesLightTableUBO[VertexProgram] || m_ProgramUsesLightTableUBO[PixelProgram])
		uploadLightTableUBO();

	// Flush user-bound UBOs (upload if dirty, bind to GL points)
	flushUserUBOs();

	setupUniforms(IDriver::VertexProgram);
	setupUniforms(IDriver::PixelProgram);
	return true;
}

void CDriverGL3::setupUniforms(TProgram program)
{
	CMaterial &mat = *_CurrentMaterial;
	IProgram *p = getProgram(program);
	if (p == NULL) return;
	IProgramDrvInfos *di = p->m_DrvInfo;
	if (di == NULL) return;

	CProgramDrvInfosGL3 *drvInfo = static_cast<CProgramDrvInfosGL3 *>(di);
	GLuint progId = drvInfo->getProgramId();

	// FIXME
	if (!progId)
		return;

	if (!m_ProgramUsesObjectUBO[program])
	{
		uint mvpIndex = p->getUniformIndex(CProgramIndex::ModelViewProjection);
		if (mvpIndex != ~0)
		{
			CMatrix mvp = _GLProjMat * _ChangeBasis * _ModelViewMatrix;
			setUniform4x4f(program, mvpIndex, mvp);
		}

		uint mvIndex = p->getUniformIndex(CProgramIndex::ModelView);
		if (mvIndex != ~0)
		{
			setUniform4x4f(program, mvIndex, _ModelViewMatrix);
		}

		uint nmIdx = p->getUniformIndex(CProgramIndex::NormalMatrix);
		if (nmIdx != ~0)
		{
			const float *mv = _ModelViewMatrix.get();
			float nm[ 3 * 3 ];
			nm[ 0 ] = mv[ 0 ];
			nm[ 1 ] = mv[ 1 ];
			nm[ 2 ] = mv[ 2 ];
			nm[ 3 ] = mv[ 4 ];
			nm[ 4 ] = mv[ 5 ];
			nm[ 5 ] = mv[ 6 ];
			nm[ 6 ] = mv[ 8 ];
			nm[ 7 ] = mv[ 9 ];
			nm[ 8 ] = mv[ 10 ];

			setUniform3x3f(program, nmIdx, nm);
		}
	}

	if (!m_ProgramUsesCameraUBO[program])
	{
		uint vmIndex = p->getUniformIndex(CProgramIndex::ViewMatrix);
		if (vmIndex != ~0)
			setUniform4x4f(program, vmIndex, _ViewMtx);
	}

	if (!m_ProgramUsesCameraUBO[program])
	{
		uint fogParamsIdx = p->getUniformIndex(CProgramIndex::FogParams);
		if (fogParamsIdx != ~0)
			nglProgramUniform2f(progId, fogParamsIdx, _FogStart, _FogEnd);

		uint fogColorIdx = p->getUniformIndex(CProgramIndex::FogColor);
		if (fogColorIdx != ~0)
			nglProgramUniform4fv(progId, fogColorIdx, 1, _CurrentFogColor);

		uint fogDensityIdx = p->getUniformIndex(CProgramIndex::FogDensity);
		if (fogDensityIdx != ~0)
			nglProgramUniform1f(progId, fogDensityIdx, _FogDensity);
	}

	if (!m_ProgramUsesMaterialUBO[program])
	{
		uint colorIndex = p->getUniformIndex(CProgramIndex::Color);
		if (colorIndex != ~0)
		{
			GLfloat glCol[ 4 ];
			CRGBA col = mat.getColor();
			glCol[ 0 ] = col.R / 255.0f;
			glCol[ 1 ] = col.G / 255.0f;
			glCol[ 2 ] = col.B / 255.0f;
			glCol[ 3 ] = col.A / 255.0f;

			nglProgramUniform4f(progId, colorIndex, glCol[ 0 ], glCol[ 1 ], glCol[ 2 ], glCol[ 3 ]);
		}

		uint alphaRefIdx = p->getUniformIndex(CProgramIndex::AlphaRef);
		if (alphaRefIdx != ~0)
			nglProgramUniform1f(progId, alphaRefIdx, mat.getAlphaTestThreshold());
	}

	if (!m_ProgramUsesObjectUBO[program])
	{
		// Compute selfIllumination (always needed, regardless of table mode)
		NLMISC::CRGBAF selfIllumination = NLMISC::CRGBAF(_AmbientGlobal);
		for (uint i = 0; i < NL_OPENGL3_MAX_LIGHT; ++i)
		{
			if (_LightEnable[i])
				selfIllumination += NLMISC::CRGBAF(_UserLight[i].getAmbiant());
		}
		selfIllumination *= NLMISC::CRGBAF(mat.getAmbient());
		if (mat.getShader() != CMaterial::LightMap) // Really?
			selfIllumination += NLMISC::CRGBAF(mat.getEmissive());
		int selfIlluminationId = p->getUniformIndex(CProgramIndex::TName(CProgramIndex::SelfIllumination));
		if (selfIlluminationId != -1)
		{
			nglProgramUniform4f(progId, selfIlluminationId, selfIllumination.R, selfIllumination.G, selfIllumination.B, 0.0f);
		}
	}

	if (m_ProgramUsesObjectUBO[program])
	{
		// Object UBO handles selfIllumination, light indices/factors, matrices — skip individual uploads

		// Material properties as individual uniforms when material UBO is not active
		// (e.g. wind tree VP uses object UBO but not material UBO)
		if (!m_ProgramUsesMaterialUBO[program])
		{
			NLMISC::CRGBAF matDiffuse = mat.isLightedVertexColor()
				? NLMISC::CRGBAF(1.0f, 1.0f, 1.0f, 1.0f)
				: NLMISC::CRGBAF(mat.getDiffuse());
			NLMISC::CRGBAF matSpecular = NLMISC::CRGBAF(mat.getSpecular());

			uint mdIdx = p->getUniformIndex(CProgramIndex::NlMaterialDiffuse);
			if (mdIdx != ~0u)
				nglProgramUniform4f(progId, mdIdx, matDiffuse.R, matDiffuse.G, matDiffuse.B, matDiffuse.A);

			uint msIdx = p->getUniformIndex(CProgramIndex::NlMaterialSpecular);
			if (msIdx != ~0u)
				nglProgramUniform4f(progId, msIdx, matSpecular.R, matSpecular.G, matSpecular.B, matSpecular.A);

			uint mshIdx = p->getUniformIndex(CProgramIndex::NlMaterialShininess);
			if (mshIdx != ~0u)
				nglProgramUniform1f(progId, mshIdx, mat.getShininess());
		}
	}
	else if (m_ProgramUsesLightTableUBO[program])
	{
		// Light table UBO path: per-object indices/factors/material

		// Per-object light indices and factors
		for (uint i = 0; i < NL_OPENGL3_MAX_LIGHT; ++i)
		{
			sint32 lightIdx;
			float lightFactor;
			if (_LightTableMode)
			{
				// Table mode: indices and factors from setLights()
				lightIdx = (sint32)_LightTableObjIndices[i];
				lightFactor = _LightTableObjFactors[i];
			}
			else
			{
				// Non-table mode: trivial mapping, slot i → UBO entry i
				lightIdx = _LightEnable[i] ? (sint32)i : -1;
				lightFactor = 1.0f;
			}

			uint idxIdx = p->getUniformIndex(CProgramIndex::TName(CProgramIndex::NlLightIndex0 + i));
			if (idxIdx != ~0u)
				nglProgramUniform1i(progId, idxIdx, lightIdx);

			uint factIdx = p->getUniformIndex(CProgramIndex::TName(CProgramIndex::NlLightFactor0 + i));
			if (factIdx != ~0u)
				nglProgramUniform1f(progId, factIdx, lightFactor);
		}

		if (!m_ProgramUsesMaterialUBO[program])
		{
			// Material properties for GPU-side light×material multiply
			NLMISC::CRGBAF matDiffuse = mat.isLightedVertexColor()
				? NLMISC::CRGBAF(1.0f, 1.0f, 1.0f, 1.0f)
				: NLMISC::CRGBAF(mat.getDiffuse());
			NLMISC::CRGBAF matSpecular = NLMISC::CRGBAF(mat.getSpecular());

			uint mdIdx = p->getUniformIndex(CProgramIndex::NlMaterialDiffuse);
			if (mdIdx != ~0u)
				nglProgramUniform4f(progId, mdIdx, matDiffuse.R, matDiffuse.G, matDiffuse.B, matDiffuse.A);

			uint msIdx = p->getUniformIndex(CProgramIndex::NlMaterialSpecular);
			if (msIdx != ~0u)
				nglProgramUniform4f(progId, msIdx, matSpecular.R, matSpecular.G, matSpecular.B, matSpecular.A);

			uint mshIdx = p->getUniformIndex(CProgramIndex::NlMaterialShininess);
			if (mshIdx != ~0u)
				nglProgramUniform1f(progId, mshIdx, mat.getShininess());
		}

		if (!m_ProgramUsesCameraUBO[program])
		{
			uint pzbIdx = p->getUniformIndex(CProgramIndex::PzbCameraPos);
			if (pzbIdx != ~0u)
				nglProgramUniform3f(progId, pzbIdx, _PZBCameraPos.x, _PZBCameraPos.y, _PZBCameraPos.z);
		}
	}
	else
	{
		// Legacy per-light uniform path
		NLMISC::CRGBAF matDiffuse = mat.isLightedVertexColor()
			? NLMISC::CRGBAF(1.0f, 1.0f, 1.0f, 1.0f)
			: NLMISC::CRGBAF(mat.getDiffuse());
		NLMISC::CRGBAF matSpecular = NLMISC::CRGBAF(mat.getSpecular());

		for (uint i = 0; i < NL_OPENGL3_MAX_LIGHT; ++i)
		{
			if (!_LightEnable[i])
				continue;

			if (_LightMode[i] == CLight::DirectionalLight)
			{
				uint ld = p->getUniformIndex(CProgramIndex::TName(CProgramIndex::Light0DirOrPos + i));
				if (ld != ~0)
				{
					CVector v = -1 * _UserLight[i].getDirection();
					nglProgramUniform3f(progId, ld, v.x, v.y, v.z);
				}
			}
			else if (_LightMode[i] == CLight::PointLight || _LightMode[i] == CLight::SpotLight)
			{
				uint lp = p->getUniformIndex(CProgramIndex::TName(CProgramIndex::Light0DirOrPos + i));
				if (lp != ~0)
				{
					CVector v = _UserLight[i].getPosition() - _PZBCameraPos;
					nglProgramUniform3f(progId, lp, v.x, v.y, v.z);
				}
			}
			else
			{
				continue; // Unknown light mode
			}

			uint ldc = p->getUniformIndex(CProgramIndex::TName(CProgramIndex::Light0ColDiff + i));
			if (ldc != ~0)
			{
				NLMISC::CRGBAF diffuse = NLMISC::CRGBAF(_UserLight[i].getDiffuse()) * matDiffuse;
				nglProgramUniform4f(progId, ldc, diffuse.R, diffuse.G, diffuse.B, 0.0f); // 1.0f?
			}

			uint lsc = p->getUniformIndex(CProgramIndex::TName(CProgramIndex::Light0ColSpec + i));
			if (lsc != ~0)
			{
				NLMISC::CRGBAF specular = NLMISC::CRGBAF(_UserLight[i].getSpecular()) * matSpecular;
				nglProgramUniform4f(progId, lsc, specular.R, specular.G, specular.B, 0.0f); // 1.0f?
			}

			uint shl = p->getUniformIndex(CProgramIndex::TName(CProgramIndex::Light0Shininess + i));
			if (shl != ~0)
			{
				nglProgramUniform1f(progId, shl, mat.getShininess());
			}

			uint lca = p->getUniformIndex(CProgramIndex::TName(CProgramIndex::Light0ConstAttn + i));
			if (lca != ~0)
			{
				nglProgramUniform1f(progId, lca, _UserLight[ i ].getConstantAttenuation());
			}

			uint lla = p->getUniformIndex(CProgramIndex::TName(CProgramIndex::Light0LinAttn + i));
			if (lla != ~0)
			{
				nglProgramUniform1f(progId, lla, _UserLight[ i ].getLinearAttenuation());
			}

			uint lqa = p->getUniformIndex(CProgramIndex::TName(CProgramIndex::Light0QuadAttn + i));
			if (lqa != ~0)
			{
				nglProgramUniform1f(progId, lqa, _UserLight[ i ].getQuadraticAttenuation());
			}

			if (_LightMode[i] == CLight::SpotLight)
			{
				uint lsd = p->getUniformIndex(CProgramIndex::TName(CProgramIndex::Light0SpotDir + i));
				if (lsd != ~0)
				{
					CVector d = _UserLight[i].getDirection();
					nglProgramUniform3f(progId, lsd, d.x, d.y, d.z);
				}

				uint lsc2 = p->getUniformIndex(CProgramIndex::TName(CProgramIndex::Light0SpotCutoff + i));
				if (lsc2 != ~0)
				{
					nglProgramUniform1f(progId, lsc2, cosf(_UserLight[i].getCutoff()));
				}

				uint lse = p->getUniformIndex(CProgramIndex::TName(CProgramIndex::Light0SpotExp + i));
				if (lse != ~0)
				{
					nglProgramUniform1f(progId, lse, _UserLight[i].getExponent());
				}
			}
		}
	}

	// Upload clip plane uniforms (eye-space plane equations) — skip when camera UBO active
	if (!m_ProgramUsesCameraUBO[program])
	{
		for (uint i = 0; i < MaxClipPlanes; ++i)
		{
			if (!_ClipPlaneEnabled[i])
				continue;
			uint cpIdx = p->getUniformIndex(CProgramIndex::TName(CProgramIndex::ClipPlane0 + i));
			if (cpIdx != ~0u)
				nglProgramUniform4f(progId, cpIdx, _ClipPlaneEye[i][0], _ClipPlaneEye[i][1], _ClipPlaneEye[i][2], _ClipPlaneEye[i][3]);
		}
	}
}

void CDriverGL3::setupInitialUniforms(IProgram *program)
{
	IProgramDrvInfos *di = program->m_DrvInfo;
	if (di != NULL)
	{
		CProgramDrvInfosGL3 *drvInfo = static_cast<CProgramDrvInfosGL3 *>(di);
		GLuint id = drvInfo->getProgramId();

		for (uint i = 0; i < std::min(_Extensions.MaxFragmentTextureImageUnits, (GLint)IDRV_PROGRAM_MAXSAMPLERS); ++i)
		{
			uint samplerIdx = program->getUniformIndex((CProgramIndex::TName)(CProgramIndex::Sampler0 + i));
			if (samplerIdx >= 0)
				nglProgramUniform1i(id, samplerIdx, i);
		}

		// Resolve and cache NlLightTable UBO block index, bind to its binding point
		GLuint lightTableBlock = nglGetUniformBlockIndex(id, "NlLightTable");
		drvInfo->setLightTableBlockIndex(lightTableBlock);
		if (lightTableBlock != GL_INVALID_INDEX)
			nglUniformBlockBinding(id, lightTableBlock, NL_BUILTIN_LIGHT_TABLE_BINDING);

		// Resolve and cache NlCamera UBO block index, bind to its binding point
		GLuint cameraBlock = nglGetUniformBlockIndex(id, "NlCamera");
		drvInfo->setCameraBlockIndex(cameraBlock);
		if (cameraBlock != GL_INVALID_INDEX)
			nglUniformBlockBinding(id, cameraBlock, NL_BUILTIN_CAMERA_BINDING);

		// Resolve and cache NlModel UBO block index, bind to its binding point
		GLuint objectBlock = nglGetUniformBlockIndex(id, "NlModel");
		drvInfo->setObjectBlockIndex(objectBlock);
		if (objectBlock != GL_INVALID_INDEX)
			nglUniformBlockBinding(id, objectBlock, NL_BUILTIN_MODEL_BINDING);

		// Resolve and cache NlMaterial UBO block index, bind to its binding point
		GLuint materialBlock = nglGetUniformBlockIndex(id, "NlMaterial");
		drvInfo->setMaterialBlockIndex(materialBlock);
		if (materialBlock != GL_INVALID_INDEX)
			nglUniformBlockBinding(id, materialBlock, NL_BUILTIN_MATERIAL_BINDING);

		// Resolve and bind user UBO blocks (VP/PP) — binding points set at link time
		IProgram::CSource *src = program->source();
		if (src)
		{
			static const sint s_UBBindingToGL[] = {
				NL_USER_VERTEX_PROGRAM_BINDING,  // UBBindingVertexProgram
				NL_USER_PIXEL_PROGRAM_BINDING,   // UBBindingPixelProgram
			};
			for (std::map<sint, NLMISC::CSmartPtr<CUniformBufferFormat> >::const_iterator
				it = src->UniformBufferFormats.begin(); it != src->UniformBufferFormats.end(); ++it)
			{
				nlassert(it->first >= 0 && it->first < UBBindingCount);
				const CUniformBufferFormat &ubf = *it->second;
				std::string blockName = ubf.Name.empty()
					? ("NlUBO" + NLMISC::toString(s_UBBindingToGL[it->first]))
					: ubf.Name;
				GLuint blockIdx = nglGetUniformBlockIndex(id, blockName.c_str());
				if (blockIdx != GL_INVALID_INDEX)
					nglUniformBlockBinding(id, blockIdx, s_UBBindingToGL[it->first]);
			}
		}
	}
}

bool CDriverGL3::initProgramPipeline()
{
	ppoId = 0;

	nglGenProgramPipelines(1, &ppoId);
	if (ppoId == 0)
		return false;

	nglBindProgramPipeline(ppoId);

	return true;
}

} // NLDRIVERGL3
} // NL3D

/* end of file */
