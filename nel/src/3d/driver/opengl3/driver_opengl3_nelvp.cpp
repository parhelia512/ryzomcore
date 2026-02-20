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

// nelvp assembly to GLSL converter for the GL3 driver.
// Parses nelvp source via CVPParser, generates GLSL 330 (SSO) or 300 es (linked)
// with constant registers packed into a UBO.

#include "stdopengl3.h"

#include <sstream>

#include "nel/3d/vertex_program.h"
#include "nel/3d/vertex_program_parse.h"
#include "nel/3d/uniform_buffer_format.h"

#include "driver_opengl3.h"
#include "driver_opengl3_program.h"
#include "driver_opengl3_uniform_buffer.h"

namespace NL3D {
namespace NLDRIVERGL3 {

// Number of nelvp constant registers
static const int NELVP_NUM_CONSTANTS = 96;
// Extra UBO slots for inverse projection matrix (4 vec4 columns) used by ecPos epilogue
static const int NELVP_INV_PROJ_BASE = 96;
static const int NELVP_UBO_VEC4_COUNT = 100;

// Output register enum values from CVPOperand::EOutputRegister
// mapped to their hardware slot indices
static const int s_OutputHWSlot[] = {
	0,  // OHPosition  -> gl_Position
	1,  // OPrimaryColor -> diffuseColor (location 3)
	2,  // OSecondaryColor -> specularColor (location 4)
	3,  // OBackFacePrimaryColor -> unsupported
	4,  // OBackFaceSecondaryColor -> unsupported
	5,  // OFogCoord -> fog (location 5)
	6,  // OPointSize -> gl_PointSize
	8,  // OTex0 -> texCoord0 (location 8)
	9,  // OTex1 -> texCoord1 (location 9)
	10, // OTex2 -> texCoord2 (location 10)
	11, // OTex3 -> texCoord3 (location 11)
	12, // OTex4 -> texCoord4 (location 12)
	13, // OTex5 -> texCoord5 (location 13)
	14, // OTex6 -> texCoord6 (location 14)
	15, // OTex7 -> texCoord7 (location 15)
};

// GLSL varying names for output registers (indexed by EOutputRegister enum)
static const char *s_OutputVaryingName[] = {
	NULL,              // OHPosition -> gl_Position (builtin, not a varying)
	"diffuseColor",    // OPrimaryColor
	"specularColor",   // OSecondaryColor
	NULL,              // OBackFacePrimaryColor (unsupported)
	NULL,              // OBackFaceSecondaryColor (unsupported)
	"fog",             // OFogCoord
	NULL,              // OPointSize -> gl_PointSize (builtin)
	"texCoord0",       // OTex0
	"texCoord1",       // OTex1
	"texCoord2",       // OTex2
	"texCoord3",       // OTex3
	"texCoord4",       // OTex4
	"texCoord5",       // OTex5
	"texCoord6",       // OTex6
	"texCoord7",       // OTex7
};

// GLSL varying locations for output registers (indexed by EOutputRegister enum)
static const int s_OutputVaryingLocation[] = {
	-1, // OHPosition -> gl_Position
	VaryingLocationDiffuseColor,   // OPrimaryColor -> 3
	VaryingLocationSpecularColor,  // OSecondaryColor -> 4
	-1, // OBackFacePrimaryColor (unsupported)
	-1, // OBackFaceSecondaryColor (unsupported)
	5,  // OFogCoord -> 5
	-1, // OPointSize -> gl_PointSize
	8,  // OTex0
	9,  // OTex1
	10, // OTex2
	11, // OTex3
	12, // OTex4
	13, // OTex5
	14, // OTex6
	15, // OTex7
};

// Swizzle component character
static inline char swizzleChar(CVPSwizzle::EComp c)
{
	static const char s[] = { 'x', 'y', 'z', 'w' };
	return s[c];
}

// Write a swizzle suffix to the stream, truncated to numComponents.
// If numComponents == 4 and the swizzle is identity, nothing is written.
static void writeSwizzle(std::stringstream &ss, const CVPSwizzle &swz, int numComponents = 4)
{
	if (numComponents == 4 && swz.isIdentity())
		return;
	ss << '.';
	for (int i = 0; i < numComponents; i++)
		ss << swizzleChar(swz.Comp[i]);
}

// Write a scalar swizzle (for scalar ops like RCP, RSQ)
static void writeScalarSwizzle(std::stringstream &ss, const CVPSwizzle &swz)
{
	// For scalar ops, only the first component matters; use .x of the swizzle
	ss << '.' << swizzleChar(swz.Comp[0]);
}

// Write a write mask suffix
static void writeWriteMask(std::stringstream &ss, uint mask)
{
	if (mask == 0xF)
		return; // Full mask, no suffix needed
	ss << '.';
	if (mask & 1) ss << 'x';
	if (mask & 2) ss << 'y';
	if (mask & 4) ss << 'z';
	if (mask & 8) ss << 'w';
}

// Count the number of components in a write mask
static int writeMaskCount(uint mask)
{
	int count = 0;
	if (mask & 1) count++;
	if (mask & 2) count++;
	if (mask & 4) count++;
	if (mask & 8) count++;
	return count;
}

// Write a source operand reference.
// numComponents controls how many swizzle components are emitted (must match
// the destination write mask width for component-wise ops).  Default 4 = full vec4.
static void writeSrcOperand(std::stringstream &ss, const CVPOperand &op, int numComponents = 4)
{
	if (op.Negate)
		ss << "(-";

	switch (op.Type)
	{
	case CVPOperand::Variable:
		ss << "R" << op.Value.VariableValue;
		break;
	case CVPOperand::Constant:
		if (op.Indexed)
			ss << "c[clamp(A0x + (" << op.Value.ConstantValue << "), 0, " << (NELVP_UBO_VEC4_COUNT - 1) << ")]";
		else
			ss << "c[" << op.Value.ConstantValue << "]";
		break;
	case CVPOperand::InputRegister:
		ss << "v" << (int)op.Value.InputRegisterValue;
		break;
	case CVPOperand::OutputRegister:
		{
			int enumVal = (int)op.Value.OutputRegisterValue;
			if (enumVal == CVPOperand::OHPosition)
				ss << "gl_Position";
			else if (enumVal == CVPOperand::OPointSize)
				ss << "gl_PointSize";
			else if (s_OutputVaryingName[enumVal])
				ss << s_OutputVaryingName[enumVal];
			else
				ss << "/* unsupported output " << enumVal << " */vec4(0.0)";
		}
		break;
	case CVPOperand::AddressRegister:
		ss << "A0x";
		break;
	default:
		ss << "/* unknown operand */vec4(0.0)";
		break;
	}

	if (op.Type != CVPOperand::AddressRegister)
		writeSwizzle(ss, op.Swizzle, numComponents);

	if (op.Negate)
		ss << ")";
}

// Write a scalar source operand (for RCP, RSQ, EXP, LOG, ARL)
static void writeScalarSrcOperand(std::stringstream &ss, const CVPOperand &op)
{
	if (op.Negate)
		ss << "(-";

	switch (op.Type)
	{
	case CVPOperand::Variable:
		ss << "R" << op.Value.VariableValue;
		break;
	case CVPOperand::Constant:
		if (op.Indexed)
			ss << "c[clamp(A0x + (" << op.Value.ConstantValue << "), 0, " << (NELVP_UBO_VEC4_COUNT - 1) << ")]";
		else
			ss << "c[" << op.Value.ConstantValue << "]";
		break;
	case CVPOperand::InputRegister:
		ss << "v" << (int)op.Value.InputRegisterValue;
		break;
	default:
		ss << "/* unknown operand */0.0";
		break;
	}

	writeScalarSwizzle(ss, op.Swizzle);

	if (op.Negate)
		ss << ")";
}

// Write a destination operand (LHS of assignment)
static void writeDestOperand(std::stringstream &ss, const CVPOperand &op)
{
	switch (op.Type)
	{
	case CVPOperand::Variable:
		ss << "R" << op.Value.VariableValue;
		break;
	case CVPOperand::OutputRegister:
		{
			int enumVal = (int)op.Value.OutputRegisterValue;
			if (enumVal == CVPOperand::OHPosition)
				ss << "gl_Position";
			else if (enumVal == CVPOperand::OPointSize)
				ss << "gl_PointSize";
			else if (s_OutputVaryingName[enumVal])
				ss << s_OutputVaryingName[enumVal];
			else
			{
				ss << "/* unsupported output " << enumVal << " */";
				return;
			}
		}
		break;
	case CVPOperand::AddressRegister:
		ss << "A0x";
		return; // No write mask for address register
	default:
		ss << "/* unknown dest */";
		return;
	}

	writeWriteMask(ss, op.WriteMask);
}

// Write a broadcast assignment for scalar result ops (DP3, DP4, RCP, RSQ, etc.)
// scalar_expr is the scalar value; dest gets it broadcast to all masked components
static void writeBroadcastAssign(std::stringstream &ss, const CVPOperand &dest,
	const std::string &scalarExpr)
{
	int maskCount = writeMaskCount(dest.WriteMask);

	if (dest.Type == CVPOperand::OutputRegister
		&& dest.Value.OutputRegisterValue == CVPOperand::OPointSize)
	{
		// gl_PointSize is a scalar
		ss << "gl_PointSize = " << scalarExpr << ";\n";
		return;
	}

	writeDestOperand(ss, dest);
	ss << " = ";

	if (dest.WriteMask == 0xF)
	{
		ss << "vec4(" << scalarExpr << ")";
	}
	else if (maskCount == 1)
	{
		ss << scalarExpr;
	}
	else
	{
		switch (maskCount)
		{
		case 2: ss << "vec2(" << scalarExpr << ")"; break;
		case 3: ss << "vec3(" << scalarExpr << ")"; break;
		case 4: ss << "vec4(" << scalarExpr << ")"; break;
		}
	}
	ss << ";\n";
}

// Convert a nelvp program to GLSL and append as a new source entry
bool CDriverGL3::convertNelvpToGLSL(CVertexProgram *program, bool linked)
{
	// Find the nelvp source
	IProgram::CSource *nelvpSrc = NULL;
	for (int i = 0; i < program->getSourceNb(); i++)
	{
		IProgram::CSource *s = program->getSource(i);
		if (s->Profile == IProgram::nelvp)
		{
			nelvpSrc = s;
			break;
		}
	}
	if (!nelvpSrc || !nelvpSrc->SourcePtr)
		return false;

	// Parse
	CVPParser parser;
	CVPParser::TProgram parsed;
	std::string parseError;
	if (!parser.parse(nelvpSrc->SourcePtr, parsed, parseError))
	{
		nlwarning("GL3: nelvp parse error: %s", parseError.c_str());
		return false;
	}

	// Analyze register usage
	bool inputUsed[16];
	bool outputUsed[CVPOperand::OutputRegisterCount];
	bool tempUsed[12];
	bool addressUsed = false;
	bool indexedConstantUsed = false;
	memset(inputUsed, 0, sizeof(inputUsed));
	memset(outputUsed, 0, sizeof(outputUsed));
	memset(tempUsed, 0, sizeof(tempUsed));

	for (size_t i = 0; i < parsed.size(); i++)
	{
		const CVPInstruction &instr = parsed[i];
		uint numSrc = instr.getNumUsedSrc();

		// Scan destination
		if (instr.Dest.Type == CVPOperand::OutputRegister)
			outputUsed[instr.Dest.Value.OutputRegisterValue] = true;
		else if (instr.Dest.Type == CVPOperand::Variable)
			tempUsed[instr.Dest.Value.VariableValue] = true;
		else if (instr.Dest.Type == CVPOperand::AddressRegister)
			addressUsed = true;

		// Scan sources
		for (uint s = 0; s < numSrc; s++)
		{
			const CVPOperand &src = instr.getSrc(s);
			if (src.Type == CVPOperand::InputRegister)
				inputUsed[src.Value.InputRegisterValue] = true;
			else if (src.Type == CVPOperand::Variable)
				tempUsed[src.Value.VariableValue] = true;
			else if (src.Type == CVPOperand::Constant && src.Indexed)
				indexedConstantUsed = true;
		}
	}

	// Build GLSL
	std::stringstream ss;

	if (linked)
		ss << "#version 300 es\n";
	else
	{
		ss << "#version 330\n";
		ss << "#extension GL_ARB_separate_shader_objects : enable\n";
	}
	ss << "\n";

	if (linked)
		ss << "precision highp float;\n\n";

	if (!linked)
		ss << "out gl_PerVertex { vec4 gl_Position; float gl_PointSize; };\n\n";

	// Input attributes
	for (int i = 0; i < 16; i++)
	{
		if (inputUsed[i])
		{
			if (!linked)
				ss << "layout(location = " << i << ") ";
			ss << "in vec4 v" << i << ";\n";
		}
	}
	ss << "\n";

	// Output varyings
	// Always output ecPos at location 0 (synthesized for fog)
	if (!linked)
		ss << "layout(location = " << VaryingLocationEcPos << ") ";
	ss << "smooth out vec4 ecPos;\n";

	for (int i = 0; i < CVPOperand::OutputRegisterCount; i++)
	{
		if (!outputUsed[i]) continue;
		if (s_OutputVaryingName[i] == NULL) continue; // gl_Position, gl_PointSize, or unsupported
		int loc = s_OutputVaryingLocation[i];
		if (loc < 0) continue;

		if (!linked)
			ss << "layout(location = " << loc << ") ";
		ss << "smooth out vec4 " << s_OutputVaryingName[i] << ";\n";
	}
	ss << "\n";

	// UBO header placeholder — insertBuiltinHeaders will insert the UBO block here
	// (the source starts with #version and #extension lines which insertBuiltinHeaders skips past)

	// Main function
	ss << "void main() {\n";

	// Temporary registers
	for (int i = 0; i < 12; i++)
	{
		if (tempUsed[i])
			ss << "vec4 R" << i << " = vec4(0.0);\n";
	}
	if (addressUsed)
		ss << "int A0x = 0;\n";
	ss << "\n";

	// Translate instructions
	for (size_t i = 0; i < parsed.size(); i++)
	{
		const CVPInstruction &instr = parsed[i];

		switch (instr.Opcode)
		{
		case CVPInstruction::MOV:
			{
				int mc = writeMaskCount(instr.Dest.WriteMask);
				writeDestOperand(ss, instr.Dest);
				ss << " = ";
				writeSrcOperand(ss, instr.Src1, mc);
				ss << ";\n";
			}
			break;

		case CVPInstruction::ARL:
			{
				ss << "A0x = int(floor(";
				writeScalarSrcOperand(ss, instr.Src1);
				ss << "));\n";
			}
			break;

		case CVPInstruction::ADD:
			{
				int mc = writeMaskCount(instr.Dest.WriteMask);
				writeDestOperand(ss, instr.Dest);
				ss << " = ";
				writeSrcOperand(ss, instr.Src1, mc);
				ss << " + ";
				writeSrcOperand(ss, instr.Src2, mc);
				ss << ";\n";
			}
			break;

		case CVPInstruction::MUL:
			{
				int mc = writeMaskCount(instr.Dest.WriteMask);
				writeDestOperand(ss, instr.Dest);
				ss << " = ";
				writeSrcOperand(ss, instr.Src1, mc);
				ss << " * ";
				writeSrcOperand(ss, instr.Src2, mc);
				ss << ";\n";
			}
			break;

		case CVPInstruction::MAD:
			{
				int mc = writeMaskCount(instr.Dest.WriteMask);
				writeDestOperand(ss, instr.Dest);
				ss << " = ";
				writeSrcOperand(ss, instr.Src1, mc);
				ss << " * ";
				writeSrcOperand(ss, instr.Src2, mc);
				ss << " + ";
				writeSrcOperand(ss, instr.Src3, mc);
				ss << ";\n";
			}
			break;

		case CVPInstruction::DP3:
			{
				std::stringstream expr;
				expr << "dot(";
				writeSrcOperand(expr, instr.Src1);
				expr << ".xyz, ";
				writeSrcOperand(expr, instr.Src2);
				expr << ".xyz)";
				writeBroadcastAssign(ss, instr.Dest, expr.str());
			}
			break;

		case CVPInstruction::DP4:
			{
				std::stringstream expr;
				expr << "dot(";
				writeSrcOperand(expr, instr.Src1);
				expr << ", ";
				writeSrcOperand(expr, instr.Src2);
				expr << ")";
				writeBroadcastAssign(ss, instr.Dest, expr.str());
			}
			break;

		case CVPInstruction::DST:
			{
				// dest = (1.0, src1.y*src2.y, src1.z, src2.w)
				ss << "{ vec4 _dst = vec4(1.0, ";
				writeSrcOperand(ss, instr.Src1);
				ss << ".y * ";
				writeSrcOperand(ss, instr.Src2);
				ss << ".y, ";
				writeSrcOperand(ss, instr.Src1);
				ss << ".z, ";
				writeSrcOperand(ss, instr.Src2);
				ss << ".w);\n";
				writeDestOperand(ss, instr.Dest);
				ss << " = _dst";
				writeWriteMask(ss, instr.Dest.WriteMask);
				ss << "; }\n";
			}
			break;

		case CVPInstruction::LIT:
			{
				// LIT: dest = (1.0, max(src.x, 0.0), (src.x > 0.0) ? pow(max(src.y, 0.0), clamp(src.w, -128, 128)) : 0.0, 1.0)
				// Use a temp to avoid evaluating the source multiple times
				ss << "{ vec4 _lit_src = ";
				writeSrcOperand(ss, instr.Src1);
				ss << ";\n";
				ss << "vec4 _lit = vec4(1.0, max(_lit_src.x, 0.0), "
				   << "(_lit_src.x > 0.0) ? exp2(clamp(_lit_src.w, -128.0, 128.0) * log2(max(_lit_src.y, 1e-30))) : 0.0, "
				   << "1.0);\n";
				writeDestOperand(ss, instr.Dest);
				ss << " = _lit";
				writeWriteMask(ss, instr.Dest.WriteMask);
				ss << "; }\n";
			}
			break;

		case CVPInstruction::MIN:
			{
				int mc = writeMaskCount(instr.Dest.WriteMask);
				writeDestOperand(ss, instr.Dest);
				ss << " = min(";
				writeSrcOperand(ss, instr.Src1, mc);
				ss << ", ";
				writeSrcOperand(ss, instr.Src2, mc);
				ss << ");\n";
			}
			break;

		case CVPInstruction::MAX:
			{
				int mc = writeMaskCount(instr.Dest.WriteMask);
				writeDestOperand(ss, instr.Dest);
				ss << " = max(";
				writeSrcOperand(ss, instr.Src1, mc);
				ss << ", ";
				writeSrcOperand(ss, instr.Src2, mc);
				ss << ");\n";
			}
			break;

		case CVPInstruction::SLT:
			{
				int mc = writeMaskCount(instr.Dest.WriteMask);
				writeDestOperand(ss, instr.Dest);
				if (mc == 1)
				{
					ss << " = (";
					writeSrcOperand(ss, instr.Src1, 1);
					ss << " < ";
					writeSrcOperand(ss, instr.Src2, 1);
					ss << ") ? 1.0 : 0.0;\n";
				}
				else
				{
					static const char *vecCast[] = { NULL, NULL, "vec2", "vec3", "vec4" };
					ss << " = " << vecCast[mc] << "(lessThan(";
					writeSrcOperand(ss, instr.Src1, mc);
					ss << ", ";
					writeSrcOperand(ss, instr.Src2, mc);
					ss << "));\n";
				}
			}
			break;

		case CVPInstruction::SGE:
			{
				int mc = writeMaskCount(instr.Dest.WriteMask);
				writeDestOperand(ss, instr.Dest);
				if (mc == 1)
				{
					ss << " = (";
					writeSrcOperand(ss, instr.Src1, 1);
					ss << " >= ";
					writeSrcOperand(ss, instr.Src2, 1);
					ss << ") ? 1.0 : 0.0;\n";
				}
				else
				{
					static const char *vecCast[] = { NULL, NULL, "vec2", "vec3", "vec4" };
					ss << " = " << vecCast[mc] << "(greaterThanEqual(";
					writeSrcOperand(ss, instr.Src1, mc);
					ss << ", ";
					writeSrcOperand(ss, instr.Src2, mc);
					ss << "));\n";
				}
			}
			break;

		case CVPInstruction::RCP:
			{
				std::stringstream expr;
				expr << "(1.0 / ";
				writeScalarSrcOperand(expr, instr.Src1);
				expr << ")";
				writeBroadcastAssign(ss, instr.Dest, expr.str());
			}
			break;

		case CVPInstruction::RSQ:
			{
				std::stringstream expr;
				expr << "inversesqrt(abs(";
				writeScalarSrcOperand(expr, instr.Src1);
				expr << "))";
				writeBroadcastAssign(ss, instr.Dest, expr.str());
			}
			break;

		case CVPInstruction::EXPP:
			{
				// EXP dest, src: dest = (2^floor(src), fract(src), 2^src, 1.0)
				ss << "{ float _exp_s = ";
				writeScalarSrcOperand(ss, instr.Src1);
				ss << ";\n";
				ss << "vec4 _exp = vec4(exp2(floor(_exp_s)), fract(_exp_s), exp2(_exp_s), 1.0);\n";
				writeDestOperand(ss, instr.Dest);
				ss << " = _exp";
				writeWriteMask(ss, instr.Dest.WriteMask);
				ss << "; }\n";
			}
			break;

		case CVPInstruction::LOG:
			{
				// LOG dest, src: dest = (floor(log2(|src|)), |src|/2^floor(log2(|src|)), log2(|src|), 1.0)
				ss << "{ float _log_a = abs(";
				writeScalarSrcOperand(ss, instr.Src1);
				ss << ");\n";
				ss << "float _log_f = log2(max(_log_a, 1e-30));\n";
				ss << "vec4 _log = vec4(floor(_log_f), _log_a * exp2(-floor(_log_f)), _log_f, 1.0);\n";
				writeDestOperand(ss, instr.Dest);
				ss << " = _log";
				writeWriteMask(ss, instr.Dest.WriteMask);
				ss << "; }\n";
			}
			break;

		default:
			ss << "/* unknown opcode " << (int)instr.Opcode << " */\n";
			break;
		}
	}

	// Epilogue: synthesize ecPos from gl_Position via inverse projection stored in c[96..99].
	// Using gl_Position ensures ecPos reflects any VP modifications (geomorphing, wind, etc.).
	// inv(P) * gl_Position = ChangeBasis * ModelView * adjustedPos = GL eye-space position.
	ss << "\n// Synthesize eye-space position for fog\n";
	ss << "ecPos = mat4(c[" << NELVP_INV_PROJ_BASE << "], c[" << (NELVP_INV_PROJ_BASE + 1)
	   << "], c[" << (NELVP_INV_PROJ_BASE + 2) << "], c[" << (NELVP_INV_PROJ_BASE + 3) << "]) * gl_Position;\n";

	ss << "}\n";

	// Create UBO format for constant registers
	NLMISC::CSmartPtr<CUniformBufferFormat> ubf = new CUniformBufferFormat();
	ubf->Name = "NlNelvpConstants";
	ubf->push("c", CUniformBufferFormat::FloatVec4, NELVP_UBO_VEC4_COUNT);

	// Create new CSource entry
	IProgram::CSource *newSrc = new IProgram::CSource();
	newSrc->Profile = linked ? IProgram::glsl300esv : IProgram::glsl330v;
	newSrc->setSource(ss.str());
	newSrc->DisplayName = "nelvp-converted";

	// Set features
	newSrc->Features.OnlyUBOs = true;
	newSrc->Features.OutputsSpecularColor = outputUsed[CVPOperand::OSecondaryColor];
	newSrc->Features.OutputsWorldSpacePosition = false; // ecPos is eye-space

	// Compute VPVertexFormat from used inputs
	uint16 vpVertexFormat = 0;
	for (int i = 0; i < 16; i++)
	{
		if (inputUsed[i])
			vpVertexFormat |= g_VertexFlags[i];
	}
	newSrc->Features.VPVertexFormat = vpVertexFormat;

	// Attach UBO format for the user VP binding
	newSrc->UniformBufferFormats[UBBindingVertexProgram] = ubf;

	// Carry over ParamIndices from nelvp source (maps friendly names to constant register indices)
	newSrc->ParamIndices = nelvpSrc->ParamIndices;

	// Add source to program
	program->addSource(newSrc);

	return true;
}

} // NLDRIVERGL3
} // NL3D
