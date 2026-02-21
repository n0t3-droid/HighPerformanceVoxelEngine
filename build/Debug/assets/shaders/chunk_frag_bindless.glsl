#version 460 core
#extension GL_ARB_bindless_texture : require

out vec4 FragColor;

in vec3 v_Normal;
in vec2 v_TexCoord;
in vec3 v_WorldPos;
flat in uint v_BlockType;

uniform sampler2D u_BlockAtlas;
uniform vec3 u_ViewPos;
uniform float u_Time;
uniform int u_RenderPass; // 0=opaque, 1=transparent
uniform vec3 u_SunDir;

int getAtlasIndex(uint blockType, vec3 normal) {
    // Runtime-generated atlas tiles (see Application.cpp), laid out 4x4:
    // 0=Dirt, 1=Grass, 2=Stone, 3=Sand, 4=Water, 5=Snow, 6=Wood, 7=Leaves
    // 8=Concrete, 9=Brick, 10=Glass, 11=Metal
    if (blockType == 1u) return 0;
    if (blockType == 2u) return 1;
    if (blockType == 3u) return 2;
    if (blockType == 4u) return 3;
    if (blockType == 5u) return 4;
    if (blockType == 6u) return 5;
    if (blockType == 7u) return 6;
    if (blockType == 8u) return 7;
    if (blockType == 9u) return 8;
    if (blockType == 10u) return 9;
    if (blockType == 11u) return 10;
    if (blockType == 12u) return 11;
    return -1;
}

void main() {
    vec3 baseColor;
    int idx = getAtlasIndex(v_BlockType, v_Normal);
    if (idx < 0) {
        baseColor = vec3(1.0, 0.0, 1.0);
    } else {
        // 4x4 atlas. v_TexCoord is 0/1 per corner.
        const vec2 inv = vec2(1.0 / 4.0, 1.0 / 4.0);
        ivec2 tile = ivec2(idx % 4, idx / 4);
        vec2 atlasUV = (v_TexCoord + vec2(tile)) * inv;
        baseColor = texture(u_BlockAtlas, atlasUV).rgb;
    }
    
    // Two-pass transparency: draw opaque first, then transparent.
    bool isTransparent = (v_BlockType == 5u) || (v_BlockType == 11u);
    if (u_RenderPass == 0 && isTransparent) discard;
    if (u_RenderPass == 1 && !isTransparent) discard;

    // Improve block colors with richer tints
    vec3 tint = vec3(1.0);
    if (v_BlockType == 2u) { // Grass top
        vec3 lush = vec3(0.28, 0.60, 0.25);
        vec3 forest = vec3(0.16, 0.50, 0.20);
        tint = mix(lush, forest, abs(sin(v_WorldPos.x * 0.05 + v_WorldPos.z * 0.03)));
    } else if (v_BlockType == 1u) { // Dirt
        vec3 rich = vec3(0.50, 0.38, 0.28);
        tint = rich;
    } else if (v_BlockType == 3u) { // Stone
        // Slight noise for stone to break tiling
        float noise = fract(sin(dot(v_WorldPos.xyz, vec3(12.9898, 78.233, 45.164))) * 43758.5453);
        tint = vec3(0.55) + noise * 0.08;
    } else if (v_BlockType == 7u) { // Wood
        tint = vec3(0.52, 0.34, 0.20);
    } else if (v_BlockType == 8u) { // Leaves
        tint = vec3(0.18, 0.55, 0.22);
    }
    
    // Mix atlas color with procedural tint heavily for "prettier" look without HD textures
    baseColor = mix(baseColor, tint, 0.35);

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

    if (v_BlockType == 5u) {
        // Water: deep ocean blue tint, clearer and shinier.
        vec3 N = normalize(v_Normal);
        vec3 V = normalize(u_ViewPos - v_WorldPos);
        vec3 L = lightDir;

        // Fresnel term (stronger at grazing angles)
        float ndv = clamp(dot(N, V), 0.0, 1.0);
        float fresnel = pow(1.0 - ndv, 3.0);

        // Specular highlight
        vec3 H = normalize(L + V);
        float spec = pow(max(dot(N, H), 0.0), 128.0);

        // Subtle animated ripples
        float ripple = sin(v_WorldPos.x * 0.4 + u_Time * 1.5) * sin(v_WorldPos.z * 0.4 + u_Time * 1.2) + 
                       sin(v_WorldPos.x * 0.9 - u_Time * 0.8) * 0.5;
        ripple = 0.5 + 0.5 * ripple;

        // More vibrant and transparent water color
        vec3 deepColor = vec3(0.02, 0.15, 0.45);
        vec3 shallowColor = vec3(0.1, 0.4, 0.65);
        vec3 baseWater = mix(deepColor, shallowColor, ripple * 0.4 + 0.2);

        lit = baseWater * (0.6 + diffuse * 0.4);

        // Sky reflection color via Fresnel
        vec3 skyColor = vec3(0.5, 0.7, 0.95);
        lit = mix(lit, skyColor, fresnel * 0.75);
        
        // Add specular sparkle
        lit += vec3(1.0) * spec * 0.8;

        // Surface (top face) is more transparent than sides to see depth.
        float isTop = step(0.95, N.y);
        alpha = mix(0.85, 0.65, isTop); // Sides opaque, top clearer
    }

    if (v_BlockType == 11u) {
        // Glass
        alpha = 0.45;
    }

    // Subtle color grade for richer contrast without heavy cost
    float luma = dot(lit, vec3(0.2126, 0.7152, 0.0722));
    lit = mix(vec3(luma), lit, 1.08);
    lit = pow(lit, vec3(0.97));
    lit = clamp(lit * 1.03, 0.0, 1.0);

    FragColor = vec4(lit, alpha);
}
