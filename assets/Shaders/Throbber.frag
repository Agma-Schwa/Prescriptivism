#version 330 core

out vec4 colour;

uniform vec2 position;
uniform mat4 projection;

// TODO: Make the radius a uniform.
void main() {
    float len = length(gl_FragCoord.xy - position - vec2(20, 20));
    if (len > 20) discard;
    colour = vec4(0.0, 1.0, 0.0, 1.0 - smoothstep(19.0, 20.0, len));
}
