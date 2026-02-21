#version 330 core

out vec3 fragPos;

void main() {
    const vec2 pos[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    vec2 p = pos[gl_VertexID];
    fragPos = normalize(vec3(p, 1.0));
    gl_Position = vec4(p, 0.0, 1.0);
}
