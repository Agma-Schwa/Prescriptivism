#version 330 core

#define pi 3.14159265359

out vec4 colour;
in vec4 vertex_colour;

uniform float r;
uniform vec2 position;
uniform mat4 rotation;

void main() {
    // Reverse the positional offset we applied in the vertex shader.
    vec2 coords = gl_FragCoord.xy + vec2(r) / 2;

    // Calclulate the distance to the center.
    float len = length(coords - position - vec2(r, r));
    float alpha = 1.0;

    // Discard anything outside the radius, and smooth the outer edge.
    if (len > r) discard;
    if (len >= r - 2) alpha = 1.0 - smoothstep(r - 2, r, len);

    // Discard anything 6 pixels away from the edge and smooth the inner edge.
    if (len < r - 6) discard;
    if (len <= r - 4) alpha = 1.0 - smoothstep(r - 4, r - 6, len);

    // Cut out a sector.
    //
    // I do not pretend to understand all of what is going on below;
    // the AI auto-completed half of this for me, and the other half
    // was achieved by trial and error.
    float angle_rot = atan(rotation[1][0], rotation[0][0]);
    float angle = atan(coords.y - position.y - r, coords.x - position.x - r) + angle_rot;
    if (angle < 0) angle += 2 * pi;
    if (angle > 7./4. * pi) discard;
    colour = vec4(1, 1, 1, alpha);
}
