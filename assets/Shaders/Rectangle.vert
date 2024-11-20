#version 330 core

layout(location = 0) in vec2 vertex;
out vec2 position;

uniform mat4 transform;

void main() {
    gl_Position = transform * vec4(vertex.xy, 0.0, 1.0);

    // The box SDF needs to be evaluated before transforms are applied, so
    // pass along the original position to the vertex shader.
    position = vertex;
}
