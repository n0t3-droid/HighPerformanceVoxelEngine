#version 460 core
#extension GL_ARB_shader_draw_parameters : enable
layout (location = 0) in uint a_PackedData;

// Uniforms
uniform mat4 u_ViewProjection;
uniform vec3 u_ChunkPos;
uniform bool u_UseDrawId;

layout(std430, binding = 0) buffer ChunkPositions {
    vec4 u_ChunkPositions[];
};

out vec3 v_Normal;
out vec2 v_TexCoord;
out vec3 v_WorldPos;
flat out uint v_BlockType;

void main() {
    // Unpack data
    // Bits 0-5    (6 bits): X position
    // Bits 6-11   (6 bits): Y position
    // Bits 12-17  (6 bits): Z position
    // Bits 18-20  (3 bits): Normal Index
    // Bit  21     (1 bit) : U coord
    // Bit  22     (1 bit) : V coord
    // Bits 23-30  (8 bits): Block Type

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

    // Extract block type
    v_BlockType = (a_PackedData >> 23) & 255u;

    // Calculate World Position
    vec3 localPos = vec3(x, y, z);

    vec3 chunkBase = u_ChunkPos;
    if (u_UseDrawId) {
        chunkBase = u_ChunkPositions[gl_DrawID].xyz;
    }

    vec3 worldPos = chunkBase + localPos;
    v_WorldPos = worldPos;
    
    gl_Position = u_ViewProjection * vec4(worldPos, 1.0);
}
