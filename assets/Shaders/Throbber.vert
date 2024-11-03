#version 330 core

layout (location = 0) in vec4 vertex; // vec2 pos

uniform mat4 projection;
uniform mat4 rotation;
uniform vec2 position;

void main() {
    vec4 rot = rotation * vertex;
    vec4 pos = vec4(
        position.x + rot.x,
        position.y + rot.y,
        0.0,
        1.0
    );

    gl_Position = projection * pos;
}
