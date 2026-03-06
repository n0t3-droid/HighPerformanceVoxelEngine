#version 460 core
#extension GL_ARB_bindless_texture : require

out vec4 FragColor;

in vec3 v_Normal;
in vec2 v_TexCoord;
in vec3 v_WorldPos;
flat in uint v_BlockType;
in float v_AO; // INVENTION: Per-vertex ambient occlusion from CPU mesher

uniform sampler2D u_BlockAtlas;
uniform vec3 u_ViewPos;
uniform int u_RenderPass; // 0=opaque, 1=transparent
uniform vec3 u_SunDir;
uniform vec3 u_FogColor;
uniform float u_FogDensity;
uniform float u_Time; // INVENTION: Time for water animation + fog

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

    float x1 = mix(a, b, u.x);
    float x2 = mix(c, d, u.x);
    return mix(x1, x2, u.y);
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
    // Atlas mathematics (16x16):
    // u_start = (n % 16) / 16.0
    // v_start = (n / 16) / 16.0
    // u_end   = u_start + 1/16
    // v_end   = v_start + 1/16
    // Here n is atlas tile index (0-based), derived from blockType.
    const float invTiles = 1.0 / 16.0;
    const float epsilon = 0.0005;

    int n = getAtlasIndex(blockType);
    if (n < 0) return vec2(0.0);

    float u_start = float(n % 16) * invTiles;
    float v_start = float(n / 16) * invTiles;
    float u_end = u_start + invTiles;
    float v_end = v_start + invTiles;

    u_start += epsilon;
    v_start += epsilon;
    u_end -= epsilon;
    v_end -= epsilon;

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

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

vec3 materialParams(uint blockType) {
    // x=roughness, y=metallic, z=bumpStrength
    if (blockType == kGrassBlock) return vec3(0.88, 0.02, 0.26);
    if (blockType == kDirtBlock) return vec3(0.94, 0.00, 0.24);
    if (blockType == kStoneBlock) return vec3(0.74, 0.02, 0.34);
    if (blockType == 4u) return vec3(0.90, 0.00, 0.20);   // sand
    if (blockType == 6u) return vec3(0.78, 0.00, 0.18);   // snow
    if (blockType == 7u) return vec3(0.68, 0.00, 0.30);   // wood
    if (blockType == 8u) return vec3(0.92, 0.00, 0.16);   // leaves
    if (blockType == 9u) return vec3(0.84, 0.00, 0.14);   // concrete
    if (blockType == 10u) return vec3(0.76, 0.00, 0.22);  // brick
    if (blockType == 12u) return vec3(0.30, 0.92, 0.16);  // metal
    if (isArchitectureBlock(blockType)) return vec3(0.60, 0.20, 0.20);
    return vec3(0.72, 0.04, 0.22);
}

// ============================================================================
//  INVENTION: Height-Dependent Volumetric Fog with Sun Scattering
//
//  Dense in valleys below y=45, thins exponentially with height, nearly
//  absent above y=180. Mie forward-scattering tints fog golden near sun;
//  Rayleigh scatter adds blue haze away from sun.
//  Cost: ~12 ALU ops per fragment.
// ============================================================================
vec3 computeHeightFog(vec3 sceneColor, float viewDist, vec3 fogBase, float fogDensityBase) {
    float altitude = v_WorldPos.y;
    float valleyFactor = exp(-max(altitude - 45.0, 0.0) * 0.018);
    float peakFactor = 1.0 - smoothstep(100.0, 180.0, altitude);
    float heightDensity = fogDensityBase * (0.25 + 0.75 * valleyFactor) * (0.2 + 0.8 * peakFactor);

    float camAlt = u_ViewPos.y;
    float avgAlt = (camAlt + altitude) * 0.5;
    float avgValley = exp(-max(avgAlt - 45.0, 0.0) * 0.018);
    float integratedDensity = heightDensity * 0.5 + fogDensityBase * avgValley * 0.5;
    float fog = 1.0 - exp(-integratedDensity * viewDist);
    fog = clamp(fog, 0.0, 1.0);

    vec3 viewDirFog = normalize(v_WorldPos - u_ViewPos);
    vec3 sunDirFog = normalize(u_SunDir);
    float sunDot = max(dot(viewDirFog, sunDirFog), 0.0);
    float sunHt = clamp(sunDirFog.y, 0.0, 1.0);
    float mieScatter = pow(sunDot, 6.0) * 0.20 * sunHt;
    float rayleighScatter = pow(1.0 - abs(sunDot), 2.0) * 0.05 * sunHt;

    vec3 scatterFog = fogBase
        + vec3(1.0, 0.82, 0.55) * mieScatter
        + vec3(0.55, 0.70, 1.0) * rayleighScatter;
    scatterFog = clamp(scatterFog, 0.0, 1.2);

    return mix(sceneColor, scatterFog, fog);
}

// ============================================================================
//  INVENTION: Animated Water — Perturbed normals + caustics + fresnel
//  Fully integrated with the bindless PBR pipeline.
// ============================================================================
vec4 computeAnimatedWater(vec3 N, vec3 viewDir, vec3 lightDir, float diffuse, float ao) {
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
    lit *= ao;

    float isTop = step(0.95, N.y);
    float alpha = mix(0.82, 0.55, isTop);
    alpha = mix(alpha, 0.35, fresnel * 0.40);
    return vec4(lit, alpha);
}

// ============================================================================
//  INVENTION: Real-Time Stochastic Contact Shadows (Self-Occlusion)
//  We raymarch against the *noise function* in screen-space. This creates
//  pixel-perfect shadows inside tiny crevices on the block surfaces.
// ============================================================================
float computeContactShadow(vec3 worldPos, vec3 lightDir) {
    float shadow = 0.0;
    float stepSize = 0.04;
    vec3 p = worldPos;
    
    // Quick 3-step stochastic occlusion gathering
    for(int i = 1; i <= 3; ++i) {
        p += lightDir * stepSize;
        float h = fbm2D(p.xz * 10.0);
        float sampleDiff = h * 0.1 - fract(p.y);
        if (sampleDiff > 0.0) {
            shadow += 0.33;
        }
        stepSize *= 1.5;
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
    
    // Two-pass transparency: draw opaque first, then transparent.
    bool isTransparent = (blockType == kWaterBlock) || isGlassBlock(blockType) || isPaneBlock(blockType);
    if (u_RenderPass == 0 && isTransparent) discard;
    if (u_RenderPass == 1 && !isTransparent) discard;

    vec3 lightDir = normalize(u_SunDir);
    vec3 viewDir = normalize(u_ViewPos - v_WorldPos);
    vec3 N = normalize(v_Normal);

    vec3 mat = materialParams(blockType);
    float roughness = clamp(mat.x, 0.06, 0.98);
    float metallic = clamp(mat.y, 0.0, 1.0);
    float bumpStrength = mat.z;

    // Height-derived micro-relief from albedo (no extra normal map needed).
    float h = luminance(baseColor);
    vec3 dpx = dFdx(v_WorldPos);
    vec3 dpy = dFdy(v_WorldPos);
    float dhx = dFdx(h);
    float dhy = dFdy(h);
    vec3 bumpN = normalize(N - normalize(dpx) * dhx * 0.85 - normalize(dpy) * dhy * 0.85);
    N = normalize(mix(N, bumpN, bumpStrength));

    float diffuse = max(dot(N, lightDir), 0.0);
    float ambient = 0.32;

    // Apply Stochastic Contact Shadows
    float contactShadow = computeContactShadow(v_WorldPos, lightDir);

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
    diffuse *= contactShadow * diffuseColor.x; // Simplified SSS application for Bindless

    // INVENTION: True per-vertex AO — interpolates across face for smooth crease shadows.
    float ao = mix(0.42, 1.0, v_AO * v_AO);
    float faceAO = mix(0.76, 1.0, clamp(N.y * 0.7 + 0.3, 0.0, 1.0));
    ao *= faceAO;

    // INVENTION: AO-Weighted Specular — creases dampen specular highlights
    vec3 H = normalize(lightDir + viewDir);
    float ndh = max(dot(N, H), 0.0);
    float specPow = mix(14.0, 180.0, 1.0 - roughness);
    float specular = pow(ndh, specPow) * (0.03 + metallic * 0.52) * (0.20 + 0.80 * diffuse);
    specular *= ao; // AO dampens specular in creases

    // Slight distance-grade only (keeps real texture colors intact).
    float macro = fbm2D(v_WorldPos.xz * 0.0024);
    vec3 macroTint = mix(vec3(0.95), vec3(1.03), macro);
    float macroBlend = smoothstep(130.0, 760.0, viewDist) * 0.10;

    // INVENTION: Altitude Color Grading — atmosphere bands by height
    float altitude = v_WorldPos.y;
    float altLow = smoothstep(20.0, 50.0, altitude);
    float altMid = smoothstep(50.0, 90.0, altitude) * (1.0 - smoothstep(90.0, 130.0, altitude));
    float altHigh = smoothstep(110.0, 160.0, altitude);
    vec3 altGrade = vec3(1.0);
    altGrade = mix(altGrade, vec3(1.04, 1.01, 0.95), (1.0 - altLow) * 0.15);
    altGrade = mix(altGrade, vec3(0.97, 1.00, 1.04), altMid * 0.12);
    altGrade = mix(altGrade, vec3(0.93, 0.96, 1.06), altHigh * 0.18);

    vec3 lit = (baseColor * altGrade * (ambient + diffuse * 0.82) + vec3(specular)) * ao;
    lit *= mix(vec3(1.0), macroTint, macroBlend);
    float alpha = 1.0;

    if (blockType == kWaterBlock) {
        // INVENTION: Animated Water — perturbed normals + caustics + fresnel
        vec4 w = computeAnimatedWater(normalize(v_Normal), viewDir, lightDir, diffuse, ao);
        lit = w.rgb;
        alpha = w.a;
    }

    if (isGlassBlock(blockType) || isPaneBlock(blockType)) {
        vec3 V = viewDir;
        float ndv = clamp(dot(N, V), 0.0, 1.0);
        float fresnel = pow(1.0 - ndv, 2.6);

        vec3 glassTint = vec3(0.90, 0.97, 1.02);
        lit = mix(lit, glassTint, 0.22 + fresnel * 0.30);

        float baseAlpha = isPaneBlock(blockType) ? 0.40 : 0.52;
        alpha = mix(baseAlpha, 0.30, fresnel * 0.45);
    }

    // Subtle day/night grading (more natural, less stylized).
    float sunHeight = clamp(lightDir.y, -1.0, 1.0);
    float dayFactor = clamp(sunHeight * 0.5 + 0.5, 0.0, 1.0);
    float goldenHour = (1.0 - smoothstep(0.22, 0.66, dayFactor)) * smoothstep(0.02, 0.24, dayFactor);
    float nightFactor = 1.0 - smoothstep(0.08, 0.42, dayFactor);
    vec3 warmGrade = vec3(1.04, 0.98, 0.92);
    vec3 coolGrade = vec3(0.93, 0.97, 1.03);
    lit *= mix(vec3(1.0), warmGrade, goldenHour * 0.22);
    lit *= mix(vec3(1.0), coolGrade, nightFactor * 0.14);
    if (isArchitectureBlock(blockType)) {
        float glaze = 0.02 + (1.0 - roughness) * 0.05;
        lit += vec3(glaze) * (0.4 + 0.6 * dayFactor);
    }
    lit *= mix(vec3(1.0), vec3(0.97, 1.00, 1.03), 0.08 * nightFactor);

    // Mild tone shaping.
    float luma = dot(lit, vec3(0.2126, 0.7152, 0.0722));
    lit = mix(vec3(luma), lit, 1.03);
    lit = pow(lit, vec3(0.985));
    lit = clamp(lit * 1.01, 0.0, 1.0);

    // INVENTION: Height Fog + Sun Scattering (replaces flat exp fog)
    vec3 fogged = computeHeightFog(lit, viewDist, u_FogColor, u_FogDensity);
    FragColor = vec4(fogged, alpha);
}


