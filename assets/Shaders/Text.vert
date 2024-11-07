#version 330 core

layout (location = 0) in vec4 vertex; // vec2 pos, vec2 tex
out vec2 tex;

uniform vec2 position;
uniform mat4 projection;
uniform float atlas_height;

void main() {
    gl_Position = projection * vec4(
        position.x + vertex.x,
        position.y + vertex.y,
        0.0,
        1.0
    );

    // Atlas width is constant, but the height might change,
    // so recompute the V coordinate based on the height.
    tex = vec2(vertex.z, vertex.w / atlas_height);
}
