#version 330 core

layout (location = 0) in vec4 vertex; // vec2 pos, vec2 tex
out vec2 tex;

uniform vec2 position;
uniform mat4 projection;

void main() {
    gl_Position = projection * vec4(
        position.x + vertex.x,
        position.y + vertex.y,
        0.0,
        1.0
    );

    tex = vertex.zw;
}
