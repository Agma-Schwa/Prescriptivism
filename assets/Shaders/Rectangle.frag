#version 330 core

out vec4 colour;
in vec2 position;

uniform vec4 in_colour;
uniform vec2 size;
uniform mat4 transform;
uniform float radius; // In pixels.

// How soft the edges should be (in pixels). Higher values could be used to simulate a drop shadow.
const float edge_softness = .5f;

// See https://iquilezles.org/articles/distfunctions.
float sdf(vec2 p, vec2 sz, float r) {
    vec2 q = abs(p) - sz + r;
    return length(max(q, 0)) - r;
}

// Adapted from https://www.shadertoy.com/view/WtdSDs.
void main() {
    // Calculate the distance to the corner of our quadrant.
    vec2 p = position - size / 2.0f;
    float distance = sdf(p, size / 2.0f, radius);

    // Smooth the result (free antialiasing).
    float a =  1.0f - smoothstep(0.0f, edge_softness * 2.0f, distance);

    // Return the resultant shape.
    colour = vec4(in_colour.rgb, min(a, in_colour.a));
}

