#version 330 core

out vec4 colour;
in vec2 position;

uniform vec4 in_colour;
uniform vec2 size;
uniform mat4 transform;
uniform float radius; // In pixels.

// How soft the edges should be (in pixels). Higher values could be used to simulate a drop shadow.
const float edgeSoftness = .5f;

// From https://iquilezles.org/articles/distfunctions
float roundedBoxSDF(vec2 CenterPosition, vec2 Size, float Radius) {
    return length(max(abs(CenterPosition) - Size + Radius, 0.0))-Radius;
}

// Adapted from https://www.shadertoy.com/view/WtdSDs.
void main() {
    // Calculate distance to edge.
    float distance = roundedBoxSDF(position - (size/2.0f), size / 2.0f, radius);

    // Smooth the result (free antialiasing).
    float smoothedAlpha =  1.0f - smoothstep(0.0f, edgeSoftness * 2.0f,distance);

    // Return the resultant shape.
    colour = vec4(in_colour.rgb, min(smoothedAlpha, in_colour.a));
}

