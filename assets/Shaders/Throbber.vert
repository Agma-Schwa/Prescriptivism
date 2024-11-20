#version 330 core

layout (location = 0) in vec4 vertex; // vec2 pos

uniform float r;
uniform mat4 transform;
uniform mat4 rotation;
uniform vec2 position;

void main() {
    vec4 rot = rotation * vertex;
    vec4 pos = vec4(
        position.x + rot.x - r / 2,
        position.y + rot.y - r / 2,
        0.0,
        1.0
    );

    gl_Position = transform * pos;
}
