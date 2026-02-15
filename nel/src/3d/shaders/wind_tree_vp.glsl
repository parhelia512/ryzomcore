// wind_tree_vp.glsl
// Hierarchical wind tree vertex program with per-vertex Gouraud lighting.
// GLSL 330 version for GL3/GLES3 driver.
//
// Preprocessor defines prepended at runtime:
//   #define NUM_POINT_LIGHTS n (0-3)
//   [#define USE_SPECULAR]
//   [#define USE_NORMALIZE]
//
// Requires: #version 330 and GL_ARB_separate_shader_objects
// (prepended by the constructor before this source)

#ifndef NUM_POINT_LIGHTS
#define NUM_POINT_LIGHTS 0
#endif

// Inputs
layout (location = 0) in vec4 vposition;
layout (location = 2) in vec4 vnormal;
layout (location = 3) in vec4 vprimaryColor;
layout (location = 8) in vec4 vtexCoord0;

// Outputs
out gl_PerVertex { vec4 gl_Position; };
smooth out vec4 vertexColor;
smooth out vec4 texCoord0;
smooth out vec4 ecPos;

// Transform
uniform mat4 modelViewProjection;
uniform mat4 modelView;

// Wind animation
uniform vec4 windLevel1;
uniform vec4 windLevel2[4];
uniform vec4 windLevel3[4];

// Lighting
uniform vec4 ambient;
uniform vec4 diffuse0;
#if NUM_POINT_LIGHTS >= 1
uniform vec4 diffuse1;
#endif
#if NUM_POINT_LIGHTS >= 2
uniform vec4 diffuse2;
#endif
#if NUM_POINT_LIGHTS >= 3
uniform vec4 diffuse3;
#endif
uniform vec4 diffuseAlpha;

#ifdef USE_SPECULAR
uniform vec4 specular0; // .w = shininess
#if NUM_POINT_LIGHTS >= 1
uniform vec4 specular1;
#endif
#if NUM_POINT_LIGHTS >= 2
uniform vec4 specular2;
#endif
#if NUM_POINT_LIGHTS >= 3
uniform vec4 specular3;
#endif
uniform vec4 sunDir;    // object-space sun direction
uniform vec4 eyePos;    // object-space eye position
#if NUM_POINT_LIGHTS >= 1
uniform vec4 plPos0;
#endif
#if NUM_POINT_LIGHTS >= 2
uniform vec4 plPos1;
#endif
#if NUM_POINT_LIGHTS >= 3
uniform vec4 plPos2;
#endif
#else
uniform vec4 dirOrPos0; // sun direction
#if NUM_POINT_LIGHTS >= 1
uniform vec4 dirOrPos1;
#endif
#if NUM_POINT_LIGHTS >= 2
uniform vec4 dirOrPos2;
#endif
#if NUM_POINT_LIGHTS >= 3
uniform vec4 dirOrPos3;
#endif
#endif

void main()
{
  // --- Wind Animation ---
  // Extract 3 hierarchy blend factors from vertex color R channel
  vec3 factors = clamp(vprimaryColor.xxx * 3.0 + vec3(0.0, -1.0, -2.0), 0.0, 1.0);
  vec4 pos = vposition;
  pos.xyz += windLevel1.xyz * factors.x;

  // Phase indices from G and B channels (0..3.99 -> int 0..3)
  vec2 phase = vprimaryColor.yz * 3.99;
  int idx2 = int(phase.x);
  pos.xyz += windLevel2[idx2].xyz * factors.y;
  int idx3 = int(phase.y);
  pos.xyz += windLevel3[idx3].xyz * factors.z;

  // --- Lighting ---
  vec3 N = vnormal.xyz;
#ifdef USE_NORMALIZE
  N = normalize(N);
#endif

  vec4 litColor = ambient;

#ifdef USE_SPECULAR
  float shininess = specular0.w;
  vec3 V = normalize(eyePos.xyz - pos.xyz);

  // Sun
  vec3 H = normalize(sunDir.xyz + V);
  float NdotL = max(dot(N, sunDir.xyz), 0.0);
  float NdotH = max(dot(N, H), 0.0);
  float specPow = NdotL > 0.0 ? pow(NdotH, shininess) : 0.0;
  litColor += NdotL * diffuse0;
  vec3 specAccum = specPow * specular0.xyz;

  #if NUM_POINT_LIGHTS >= 1
  {
    vec3 L = normalize(plPos0.xyz - pos.xyz);
    H = normalize(L + V);
    NdotL = max(dot(N, L), 0.0);
    NdotH = max(dot(N, H), 0.0);
    specPow = NdotL > 0.0 ? pow(NdotH, shininess) : 0.0;
    litColor += NdotL * diffuse1;
    specAccum += specPow * specular1.xyz;
  }
  #endif
  #if NUM_POINT_LIGHTS >= 2
  {
    vec3 L = normalize(plPos1.xyz - pos.xyz);
    H = normalize(L + V);
    NdotL = max(dot(N, L), 0.0);
    NdotH = max(dot(N, H), 0.0);
    specPow = NdotL > 0.0 ? pow(NdotH, shininess) : 0.0;
    litColor += NdotL * diffuse2;
    specAccum += specPow * specular2.xyz;
  }
  #endif
  #if NUM_POINT_LIGHTS >= 3
  {
    vec3 L = normalize(plPos2.xyz - pos.xyz);
    H = normalize(L + V);
    NdotL = max(dot(N, L), 0.0);
    NdotH = max(dot(N, H), 0.0);
    specPow = NdotL > 0.0 ? pow(NdotH, shininess) : 0.0;
    litColor += NdotL * diffuse3;
    specAccum += specPow * specular3.xyz;
  }
  #endif

  // Combine: diffuse with material alpha, plus specular (no hardware color sum in GL3)
  vertexColor = litColor * diffuseAlpha.zzzx + diffuseAlpha.xxxw + vec4(specAccum, 0.0);

#else // Non-specular

  // Sun (directional)
  litColor += max(dot(N, dirOrPos0.xyz), 0.0) * diffuse0;

  #if NUM_POINT_LIGHTS >= 1
  litColor += max(dot(N, normalize(dirOrPos1.xyz - pos.xyz)), 0.0) * diffuse1;
  #endif
  #if NUM_POINT_LIGHTS >= 2
  litColor += max(dot(N, normalize(dirOrPos2.xyz - pos.xyz)), 0.0) * diffuse2;
  #endif
  #if NUM_POINT_LIGHTS >= 3
  litColor += max(dot(N, normalize(dirOrPos3.xyz - pos.xyz)), 0.0) * diffuse3;
  #endif

  vertexColor = litColor * diffuseAlpha.zzzx + diffuseAlpha.xxxw;

#endif // USE_SPECULAR

  // --- Transform & Output ---
  gl_Position = modelViewProjection * pos;
  texCoord0 = vtexCoord0;
  ecPos = modelView * pos; // eye-space position for FP fog computation
}
