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

#ifndef NL3D_GPU_SKIN_VP_H
#define NL3D_GPU_SKIN_VP_H

#include "nel/misc/types_nl.h"
#include "nel/3d/vertex_program.h"

namespace NL3D {

/// Maximum bones supported by GPU skinning UBO.
/// 80 bones * 3 vec4 = 240 vec4 entries = 3840 bytes.
/// Total UBO: 8 (morphThreshold + morphAlpha) + 3840 = 3848 bytes.
/// Well within the GL 3.3 minimum of 16KB.
#define NL3D_GPU_SKIN_MAX_BONES 80

/// Get the singleton GPU skinning VP insert program.
/// Created lazily on first call. Shared by all skinned meshes.
/// The returned program uses the glsl3vi profile.
CVertexProgram *getGPUSkinInsertVP();

} // NL3D

#endif // NL3D_GPU_SKIN_VP_H

/* end of file */
