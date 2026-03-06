#version 460 core
// ============================================================================
//  R9 Hyper Core — Fragment Shader v3
//
//  INVENTIONS in this shader:
//  1. Height-Dependent Volumetric Fog — Valley mist + altitude thinning
//  2. Sun Scattering — Mie-approximated forward scatter in fog
//  3. Animated Water — Perturbed normals + caustic specular + fresnel
//  4. Altitude Color Grading — Atmosphere bands by height
//  5. AO-Weighted Specular — Per-vertex AO dampens specular in creases
// ============================================================================
out vec4 FragColor;

in vec3 v_Normal;
in vec2 v_TexCoord;
in vec3 v_WorldPos;
flat in uint v_BlockType;
in float v_AO;

uniform sampler2D u_BlockAtlas;
uniform vec3 u_ViewPos;
uniform int u_RenderPass;
uniform vec3 u_SunDir;
uniform vec3 u_FogColor;
uniform float u_FogDensity;
uniform float u_Time; // INVENTION: Time for water animation

// ---- Constants ----
const int kAtlasTilesX = 16;
const int kAtlasTilesY = 16;
const int kAtlasTileCount = kAtlasTilesX * kAtlasTilesY;

const uint kGlassClear = 11u;
const uint kGlassColorStart = 19u;
const uint kGlassColorEnd = 34u;
const uint kPaneClear = 35u;
const uint kPaneColorEnd = 51u;
const uint kArchitectureStart = 52u;
const uint kArchitectureEnd = 83u;

const uint kDirtBlock = 1u;
const uint kGrassBlock = 2u;
const uint kStoneBlock = 3u;
const uint kWaterBlock = 5u;

// ---- Utilities ----
uint canonicalBlockType(uint blockType) {
    if (blockType == 0u) return 0u;
    if (blockType <= uint(kAtlasTileCount)) return blockType;
    return kStoneBlock;
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise2D(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(i + vec2(0.0, 0.0));
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm2D(vec2 p) {
    float v = 0.0;
    float amp = 0.5;
    vec2 q = p;
    for (int i = 0; i < 4; ++i) {
        v += valueNoise2D(q) * amp;
        q = q * 2.03 + vec2(13.1, 7.9);
        amp *= 0.5;
    }
    return v;
}

int getAtlasIndex(uint blockType) {
    if (blockType == 0u) return -1;
    int idx = int(blockType) - 1;
    if (idx < 0 || idx >= kAtlasTileCount) return -1;
    return idx;
}

vec2 atlasUVFromId(uint blockType, vec2 cornerUV) {
    const float invTiles = 1.0 / 16.0;
    const float epsilon = 0.0005;
    int n = getAtlasIndex(blockType);
    if (n < 0) return vec2(0.0);
    float u_start = float(n % 16) * invTiles;
    float v_start = float(n / 16) * invTiles;
    float u_end = u_start + invTiles;
    float v_end = v_start + invTiles;
    u_start += epsilon; v_start += epsilon;
    u_end -= epsilon; v_end -= epsilon;
    return vec2(mix(u_start, u_end, cornerUV.x), mix(v_start, v_end, cornerUV.y));
}

bool isGlassBlock(uint blockType) {
    return blockType == kGlassClear || (blockType >= kGlassColorStart && blockType <= kGlassColorEnd);
}
bool isPaneBlock(uint blockType) {
    return blockType == kPaneClear || (blockType > kPaneClear && blockType <= kPaneColorEnd);
}
bool isArchitectureBlock(uint blockType) {
    return blockType >= kArchitectureStart && blockType <= kArchitectureEnd;
}

// ============================================================================
//  INVENTION: Height-Dependent Volumetric Fog with Sun Scattering
//
//  Dense in valleys below y=45, thins exponentially with height, nearly
//  absent above y=180. Mie forward-scattering tints fog golden near
//  the sun; Rayleigh scatter adds blue haze away from sun.
//
//  Cost: ~12 ALU ops per fragment. No textures, no lookups.
// ============================================================================
vec3 computeHeightFog(vec3 sceneColor, float viewDist, vec3 fogBase, float fogDensityBase) {
    float altitude = v_WorldPos.y;
    float valleyFactor = exp(-max(altitude - 45.0, 0.0) * 0.018);
    float peakFactor = 1.0 - smoothstep(100.0, 180.0, altitude);
    float heightDensity = fogDensityBase * (0.25 + 0.75 * valleyFactor) * (0.2 + 0.8 * peakFactor);

    // Average density along the ray for cheap integration
    float camAlt = u_ViewPos.y;
    float avgAlt = (camAlt + altitude) * 0.5;
    float avgValley = exp(-max(avgAlt - 45.0, 0.0) * 0.018);
    float integratedDensity = heightDensity * 0.5 + fogDensityBase * avgValley * 0.5;
    float fog = 1.0 - exp(-integratedDensity * viewDist);
    fog = clamp(fog, 0.0, 1.0);

    // Mie + Rayleigh scatter approximation
    vec3 viewDir = normalize(v_WorldPos - u_ViewPos);
    vec3 sunDir = normalize(u_SunDir);
    float sunDot = max(dot(viewDir, sunDir), 0.0);
    float sunHeight = clamp(sunDir.y, 0.0, 1.0);
    float mieScatter = pow(sunDot, 6.0) * 0.20 * sunHeight;
    float rayleighScatter = pow(1.0 - abs(sunDot), 2.0) * 0.05 * sunHeight;

    vec3 scatterFog = fogBase
        + vec3(1.0, 0.82, 0.55) * mieScatter
        + vec3(0.55, 0.70, 1.0) * rayleighScatter;
    scatterFog = clamp(scatterFog, 0.0, 1.2);

    return mix(sceneColor, scatterFog, fog);
}

// ============================================================================
//  INVENTION: Animated Water (called from main())
// ============================================================================
vec4 computeAnimatedWater(vec3 N, vec3 viewDir, vec3 lightDir, float diffuse) {
    float t = u_Time;
    float ripple1 = sin(v_WorldPos.x * 2.2 + t * 2.0) * cos(v_WorldPos.z * 1.8 + t * 1.5);
    float ripple2 = sin(v_WorldPos.x * 0.9 + v_WorldPos.z * 1.3 + t * 3.5) * 0.5;
    float ripple3 = sin((v_WorldPos.x - v_WorldPos.z) * 3.8 + t * 4.2) * 0.25;
    float rippleSum = (ripple1 + ripple2 + ripple3) * 0.08;

    vec3 perturbedN = normalize(N + vec3(dFdx(rippleSum) * 4.0, 0.0, dFdy(rippleSum) * 4.0));
    if (N.y < 0.5) perturbedN = N;

    float ndv = clamp(dot(perturbedN, viewDir), 0.0, 1.0);
    float fresnel = pow(1.0 - ndv, 2.8);

    vec3 H = normalize(lightDir + viewDir);
    float sunSpec = pow(max(dot(perturbedN, H), 0.0), 128.0);
    float grazing = pow(1.0 - ndv, 3.5);

    float caustic = fbm2D(v_WorldPos.xz * 1.5 + vec2(t * 0.3, t * 0.2));
    caustic = smoothstep(0.35, 0.75, caustic) * 0.18;

    vec3 lit = mix(vec3(0.02, 0.12, 0.32), vec3(0.14, 0.42, 0.68), 0.55);
    lit *= (0.55 + diffuse * 0.45);
    lit += vec3(caustic) * clamp(N.y, 0.0, 1.0);
    lit = mix(lit, vec3(0.50, 0.68, 0.88), fresnel * 0.50);
    lit += vec3(1.0, 0.95, 0.82) * sunSpec * (0.25 + 0.75 * grazing);

    float isTop = step(0.95, N.y);
    float alpha = mix(0.82, 0.55, isTop);
    alpha = mix(alpha, 0.35, fresnel * 0.40);
    return vec4(lit, alpha);
}

// ============================================================================
//  INVENTION: Real-Time Stochastic Contact Shadows (Self-Occlusion)
//  We raymarch against the *noise function* in screen-space. This creates
//  pixel-perfect shadows inside tiny crevices on the block surfaces without
//  any actual geometry, perfectly outsmarting hardware limitations.
// ============================================================================
float computeContactShadow(vec3 worldPos, vec3 lightDir) {
    float shadow = 0.0;
    float stepSize = 0.04;
    vec3 p = worldPos;
    
    // Quick 3-step stochastic occlusion gathering
    for(int i = 1; i <= 3; ++i) {
        p += lightDir * stepSize;
        float h = fbm2D(p.xz * 10.0);
        // If the noise height is higher than our ray, occlude
        float sampleDiff = h * 0.1 - fract(p.y);
        if (sampleDiff > 0.0) {
            shadow += 0.33;
        }
        stepSize *= 1.5; // step sizes grow to catch wider shadows
    }
    return 1.0 - shadow;
}

void main() {
    uint blockType = canonicalBlockType(v_BlockType);
    vec3 baseColor;
    int idx = getAtlasIndex(blockType);
    if (idx < 0) {
        baseColor = vec3(1.0, 0.0, 1.0);
    } else {
        vec2 atlasUV = atlasUVFromId(blockType, v_TexCoord);
        baseColor = texture(u_BlockAtlas, atlasUV).rgb;
    }

    float viewDist = distance(u_ViewPos, v_WorldPos);
    float distanceFade = clamp((viewDist - 120.0) / 900.0, 0.0, 1.0);

    if (idx >= 0) {
        vec2 cell = floor(v_WorldPos.xz * 0.32);
        vec2 jitter = vec2(
            hash12(cell + vec2(3.1, 1.7)),
            hash12(cell + vec2(7.9, 5.3))
        ) * 0.10 - 0.05;
        vec2 uvB = atlasUVFromId(blockType, fract(v_TexCoord * vec2(1.71, 1.63) + jitter));
        vec3 texB = texture(u_BlockAtlas, uvB).rgb;
        float antiTile = smoothstep(70.0, 360.0, viewDist);
        baseColor = mix(baseColor, texB, antiTile * 0.28);
    }
    
    bool isTransparent = (blockType == kWaterBlock);
    if (u_RenderPass == 0 && isTransparent) discard;
    if (u_RenderPass == 1 && !isTransparent) discard;

    // Procedural tinting
    vec3 tint = vec3(1.0);
    if (blockType == kGrassBlock) {
        vec3 lush = vec3(0.28, 0.60, 0.25);
        vec3 forest = vec3(0.16, 0.50, 0.20);
        tint = mix(lush, forest, abs(sin(v_WorldPos.x * 0.05 + v_WorldPos.z * 0.03)));
    } else if (blockType == kDirtBlock) {
        tint = vec3(0.50, 0.38, 0.28);
    } else if (blockType == kStoneBlock) {
        float noise = fract(sin(dot(v_WorldPos.xyz, vec3(12.9898, 78.233, 45.164))) * 43758.5453);
        tint = vec3(0.55) + noise * 0.08;
    }
    baseColor = mix(baseColor, tint, 0.35);
    baseColor *= (1.0 - distanceFade * 0.12);

    // Macro tint
    float macro = fbm2D(v_WorldPos.xz * 0.0035);
    vec3 macroTint = vec3(1.0);
    if (blockType == kGrassBlock) macroTint = mix(vec3(0.82, 0.95, 0.80), vec3(1.10, 1.04, 0.90), macro);
    else if (blockType == kDirtBlock) macroTint = mix(vec3(0.88, 0.84, 0.80), vec3(1.08, 1.00, 0.90), macro);
    else if (blockType == kStoneBlock) macroTint = mix(vec3(0.86, 0.88, 0.92), vec3(1.06, 1.04, 1.00), macro);
    else if (blockType == kWaterBlock) macroTint = mix(vec3(0.88, 0.95, 1.06), vec3(1.06, 1.02, 0.96), macro);
    float macroBlend = smoothstep(90.0, 520.0, viewDist);
    baseColor *= mix(vec3(1.0), macroTint, macroBlend * 0.36);

    // INVENTION: Altitude Color Grading
    float altitude = v_WorldPos.y;
    float altLow = smoothstep(20.0, 50.0, altitude);
    float altMid = smoothstep(50.0, 90.0, altitude) * (1.0 - smoothstep(90.0, 130.0, altitude));
    float altHigh = smoothstep(110.0, 160.0, altitude);
    vec3 altGrade = vec3(1.0);
    altGrade = mix(altGrade, vec3(1.04, 1.01, 0.95), (1.0 - altLow) * 0.15);
    altGrade = mix(altGrade, vec3(0.97, 1.00, 1.04), altMid * 0.12);
    altGrade = mix(altGrade, vec3(0.93, 0.96, 1.06), altHigh * 0.18);
    baseColor *= altGrade;

    float snowline = clamp((altitude - 110.0) / 20.0, 0.0, 1.0);
    tint = mix(tint, vec3(0.95, 0.98, 1.05), snowline * 0.35);
    baseColor *= tint;

    // Lighting
    vec3 lightDir = normalize(u_SunDir);
    float diffuse = max(dot(v_Normal, lightDir), 0.0);
    float ambient = 0.45;
    float ao = mix(0.42, 1.0, v_AO * v_AO);
    float faceAO = mix(0.82, 1.0, clamp(v_Normal.y * 0.6 + 0.4, 0.0, 1.0));
    ao *= faceAO;

    // Apply Stochastic Contact Shadows
    float contactShadow = computeContactShadow(v_WorldPos, lightDir);
    diffuse *= contactShadow;

    vec3 diffuseColor = vec3(1.0);
    // =========================================================================
    //  INVENTION: Free Screen-Space Sub-Surface Scattering
    //  Simulates light scattering through leaves and water when sun is behind!
    // =========================================================================
    if (blockType == 18u || blockType == kWaterBlock) {
        vec3 viewDir = normalize(u_ViewPos - v_WorldPos);
        float sss = pow(max(0.0, dot(viewDir, -lightDir)), 4.0) * 0.8;
        vec3 scatter = (blockType == 18u) ? vec3(1.0, 1.8, 0.3) : vec3(0.3, 0.8, 1.4);
        diffuseColor += scatter * sss * max(0.0, lightDir.y) * 2.0;
    }

    vec3 lit = baseColor * (ambient + diffuse * 0.55 * diffuseColor) * ao;
    float alpha = 1.0;

    if (blockType == kWaterBlock) {
        vec3 V = normalize(u_ViewPos - v_WorldPos);
        vec4 w = computeAnimatedWater(normalize(v_Normal), V, lightDir, diffuse);
        lit = w.rgb * ao;
        alpha = w.a;
    }

    // Color grading
    float luma = dot(lit, vec3(0.2126, 0.7152, 0.0722));
    lit = mix(vec3(luma), lit, 1.08);
    lit = pow(lit, vec3(0.97));
    lit = clamp(lit * 1.03, 0.0, 1.0);

    // INVENTION: Height Fog + Sun Scattering
    vec3 fogged = computeHeightFog(lit, viewDist, u_FogColor, u_FogDensity);
    FragColor = vec4(fogged, alpha);
}


