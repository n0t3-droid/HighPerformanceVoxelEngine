#version 460 core
#extension GL_ARB_bindless_texture : require

out vec4 FragColor;

in vec3 v_Normal;
in vec2 v_TexCoord;
in vec3 v_WorldPos;
flat in uint v_BlockType;

uniform sampler2D u_BlockAtlas;
uniform vec3 u_ViewPos;
uniform int u_RenderPass; // 0=opaque, 1=transparent
uniform vec3 u_SunDir;

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

    // Improve block colors with richer tints
    vec3 tint = vec3(1.0);
    if (blockType == kGrassBlock) {
        vec3 lush = vec3(0.28, 0.60, 0.25);
        vec3 forest = vec3(0.16, 0.50, 0.20);
        tint = mix(lush, forest, abs(sin(v_WorldPos.x * 0.05 + v_WorldPos.z * 0.03)));
    } else if (blockType == kDirtBlock) {
        vec3 rich = vec3(0.50, 0.38, 0.28);
        tint = rich;
    } else if (blockType == kStoneBlock) {
        // Slight noise for stone to break tiling
        float noise = fract(sin(dot(v_WorldPos.xyz, vec3(12.9898, 78.233, 45.164))) * 43758.5453);
        tint = vec3(0.55) + noise * 0.08;
    }
    
    // Mix atlas color with procedural tint heavily for "prettier" look without HD textures
    baseColor = mix(baseColor, tint, 0.35);

    float macro = fbm2D(v_WorldPos.xz * 0.0035);
    vec3 macroTint = vec3(1.0);
    if (blockType == kGrassBlock) {
        macroTint = mix(vec3(0.82, 0.95, 0.80), vec3(1.10, 1.04, 0.90), macro);
    } else if (blockType == kDirtBlock) {
        macroTint = mix(vec3(0.88, 0.84, 0.80), vec3(1.08, 1.00, 0.90), macro);
    } else if (blockType == kStoneBlock) {
        macroTint = mix(vec3(0.86, 0.88, 0.92), vec3(1.06, 1.04, 1.00), macro);
    } else if (blockType == kWaterBlock) {
        macroTint = mix(vec3(0.88, 0.95, 1.06), vec3(1.06, 1.02, 0.96), macro);
    }
    float macroBlend = smoothstep(90.0, 520.0, viewDist);
    baseColor *= mix(vec3(1.0), macroTint, macroBlend * 0.36);

    // Cool, desaturated tint near snowline
    float snowline = clamp((v_WorldPos.y - 110.0) / 20.0, 0.0, 1.0);
    vec3 snowTint = vec3(0.95, 0.98, 1.05);
    tint = mix(tint, snowTint, snowline * 0.35);
    baseColor *= tint;

    // Simple directional lighting (animated sun)
    vec3 lightDir = normalize(u_SunDir);
    float diffuse = max(dot(v_Normal, lightDir), 0.0);
    float ambient = 0.45;

    // Micro AO by face orientation (cheap, helps readability)
    float ao = mix(0.78, 1.0, clamp(v_Normal.y * 0.6 + 0.4, 0.0, 1.0));

    vec3 lit = baseColor * (ambient + diffuse * 0.55) * ao;
    float alpha = 1.0;

    if (blockType == kWaterBlock) {
        // Water: simple fade look (no animation).
        vec3 N = normalize(v_Normal);
        vec3 V = normalize(u_ViewPos - v_WorldPos);
        float ndv = clamp(dot(N, V), 0.0, 1.0);
        float fresnel = pow(1.0 - ndv, 2.2);
        vec3 H = normalize(lightDir + V);
        float sunSpec = pow(max(dot(N, H), 0.0), 96.0);
        float grazing = pow(1.0 - ndv, 3.0);

        vec3 deepColor = vec3(0.03, 0.16, 0.38);
        vec3 surfaceColor = vec3(0.16, 0.46, 0.72);
        lit = mix(deepColor, surfaceColor, 0.58);
        lit *= (0.58 + diffuse * 0.42);
        lit = mix(lit, vec3(0.55, 0.72, 0.90), fresnel * 0.42);
        lit += vec3(1.0, 0.95, 0.84) * sunSpec * (0.20 + 0.80 * grazing);

        // Surface (top face) is clearer than sides.
        float isTop = step(0.95, N.y);
        alpha = mix(0.84, 0.60, isTop);
        alpha = mix(alpha, 0.38, fresnel * 0.38);
    }

    if (isGlassBlock(blockType) || isPaneBlock(blockType)) {
        vec3 N = normalize(v_Normal);
        vec3 V = normalize(u_ViewPos - v_WorldPos);
        float ndv = clamp(dot(N, V), 0.0, 1.0);
        float fresnel = pow(1.0 - ndv, 2.6);

        vec3 glassTint = vec3(0.90, 0.97, 1.02);
        lit = mix(lit, glassTint, 0.22 + fresnel * 0.30);

        float baseAlpha = isPaneBlock(blockType) ? 0.40 : 0.52;
        alpha = mix(baseAlpha, 0.30, fresnel * 0.45);
    }

    // Sun-dependent cinematic grade (very cheap): warm dusk/dawn + cool night.
    float sunHeight = clamp(lightDir.y, -1.0, 1.0);
    float dayFactor = clamp(sunHeight * 0.5 + 0.5, 0.0, 1.0);
    float goldenHour = (1.0 - smoothstep(0.22, 0.66, dayFactor)) * smoothstep(0.02, 0.24, dayFactor);
    float nightFactor = 1.0 - smoothstep(0.08, 0.42, dayFactor);
    vec3 warmGrade = vec3(1.08, 0.96, 0.86);
    vec3 coolGrade = vec3(0.86, 0.92, 1.04);
    lit *= mix(vec3(1.0), warmGrade, goldenHour * 0.38);
    lit *= mix(vec3(1.0), coolGrade, nightFactor * 0.22);

    // Subtle color grade for richer contrast without heavy cost
    float luma = dot(lit, vec3(0.2126, 0.7152, 0.0722));
    lit = mix(vec3(luma), lit, 1.08);
    lit = pow(lit, vec3(0.97));
    lit = clamp(lit * 1.03, 0.0, 1.0);

    FragColor = vec4(lit, alpha);
}
