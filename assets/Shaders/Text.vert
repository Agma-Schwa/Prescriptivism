#version 330 core

layout (location = 0) in vec4 vertex; // vec2 pos, vec2 tex
out vec2 tex;

uniform mat4 transform;
uniform float atlas_height;

void main() {
    gl_Position = transform * vec4(vertex.xy, 0.0, 1.0);

    // Atlas width is constant, but the height might change,
    // so recompute the V coordinate based on the height.
    tex = vec2(vertex.z, vertex.w / atlas_height);
}
