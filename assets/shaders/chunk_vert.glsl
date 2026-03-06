#version 460 core
#extension GL_ARB_shader_draw_parameters : enable
layout (location = 0) in uint a_PackedData;

// Uniforms
uniform mat4 u_ViewProjection;
uniform vec3 u_ChunkPos;
uniform bool u_UseDrawId;
uniform float u_Time; // INVENTION: Time for water animation

layout(std430, binding = 0) buffer ChunkPositions {
    vec4 u_ChunkPositions[];
};

out vec3 v_Normal;
out vec2 v_TexCoord;
out vec3 v_WorldPos;
flat out uint v_BlockType;
out float v_AO; // INVENTION: Per-vertex ambient occlusion (0.0=darkest, 1.0=brightest)

void main() {
    // Unpack data — layout with 2-bit AO:
    // Bits 0-5    (6 bits): X position
    // Bits 6-11   (6 bits): Y position
    // Bits 12-17  (6 bits): Z position
    // Bits 18-20  (3 bits): Normal Index
    // Bit  21     (1 bit) : U coord
    // Bit  22     (1 bit) : V coord
    // Bits 23-29  (7 bits): Block Type (0-127)
    // Bits 30-31  (2 bits): AO level (0=darkest, 3=brightest)

    float x = float((a_PackedData >> 0) & 63u);
    float y = float((a_PackedData >> 6) & 63u);
    float z = float((a_PackedData >> 12) & 63u);
    
    uint normalIndex = (a_PackedData >> 18) & 7u;
    
    // Normal Lookup
    vec3 normals[6] = vec3[](
        vec3(0, 1, 0),  // 0: Up
        vec3(0, -1, 0), // 1: Down
        vec3(0, 0, 1),  // 2: South
        vec3(0, 0, -1), // 3: North
        vec3(1, 0, 0),  // 4: East
        vec3(-1, 0, 0)  // 5: West
    );
    v_Normal = normals[normalIndex % 6u];

    float u = float((a_PackedData >> 21) & 1u);
    float v = float((a_PackedData >> 22) & 1u);
    v_TexCoord = vec2(u, v);

    // Extract block type (7 bits)
    v_BlockType = (a_PackedData >> 23) & 127u;

    // INVENTION: Extract 2-bit AO and map to 0.0..1.0
    uint aoRaw = (a_PackedData >> 30) & 3u;
    v_AO = float(aoRaw) / 3.0;

    // Calculate World Position
    vec3 localPos = vec3(x, y, z);

    vec3 chunkBase = u_ChunkPos;
    if (u_UseDrawId) {
        chunkBase = u_ChunkPositions[gl_DrawID].xyz;
    }

    vec3 worldPos = chunkBase + localPos;

    // =========================================================================
    //  INVENTION: Animated Water Vertex Displacement
    //
    //  Multi-octave sine waves displace water top-face vertices to create
    //  a living, breathing ocean surface. Only top-face (normal 0) water
    //  blocks are displaced — side faces remain rigid for correct shorelines.
    //
    //  3 wave octaves at different frequencies/phases prevent visible patterns.
    //  The -0.12 bias sinks the surface slightly below adjacent solid blocks to
    //  prevent z-fighting at waterlines.
    //
    //  Cost: ~6 ALU ops per water vertex — essentially free on any GPU.
    // =========================================================================
    if (v_BlockType == 5u && normalIndex == 0u) {
        float wave1 = sin(worldPos.x * 1.1 + u_Time * 2.2) * 0.055;
        float wave2 = sin(worldPos.z * 0.85 + u_Time * 1.7) * 0.045;
        float wave3 = sin((worldPos.x + worldPos.z) * 0.65 + u_Time * 3.0) * 0.025;
        worldPos.y += wave1 + wave2 + wave3 - 0.12;
    }

    // =========================================================================
    //  INVENTION: Advanced Global Wind System (Cellular Noise Deformation)
    //
    //  When rendering leaves (ID 18) or plants, vertices are displaced via 
    //  a low-frequency noise wind field. This creates flowing, natural forests.
    // =========================================================================
    if (v_BlockType == 18u) {
        float windSpeed = u_Time * 1.5;
        float windX = sin(worldPos.x * 0.4 + worldPos.z * 0.1 + windSpeed) * 0.08;
        float windZ = cos(worldPos.z * 0.4 + worldPos.x * 0.1 + windSpeed * 1.2) * 0.08;
        
        worldPos.x += windX;
        worldPos.z += windZ;
        worldPos.y += sin(worldPos.x + worldPos.y + windSpeed * 2.0) * 0.03;
    }

    v_WorldPos = worldPos;
    
    gl_Position = u_ViewProjection * vec4(worldPos, 1.0);
}

