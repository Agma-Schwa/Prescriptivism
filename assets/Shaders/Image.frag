#version 330 core

in vec2 tex;
out vec4 colour;

uniform sampler2D sampler;

void main() {
    colour = texture(sampler, tex);
}
