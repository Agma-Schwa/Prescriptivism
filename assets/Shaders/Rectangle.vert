#version 330 core

layout(location = 0) in vec2 vertex;

uniform mat4 projection;
uniform vec2 position;

void main() {
    gl_Position = projection * vec4(vertex.xy + position, 0.0, 1.0);
}
