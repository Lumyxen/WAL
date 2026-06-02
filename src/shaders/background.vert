#version 450

layout(location = 0) out vec3 fragColor;

vec2 positions[6] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0,  1.0)
);

vec3 colors[6] = vec3[](
    vec3(0.04, 0.07, 0.14),
    vec3(0.10, 0.42, 0.56),
    vec3(0.75, 0.36, 0.25),
    vec3(0.04, 0.07, 0.14),
    vec3(0.75, 0.36, 0.25),
    vec3(0.12, 0.18, 0.48)
);

void main()
{
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    fragColor = colors[gl_VertexIndex];
}
