#version 330 core

in vec2 tex;
out vec4 colour;

uniform sampler2D sampler;
uniform vec4 text_colour;

void main() {
    colour = text_colour * vec4(1.0, 1.0, 1.0, texture(sampler, tex).r);
}

