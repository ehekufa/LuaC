#version 450
layout(location = 0) in vec2 in_position;
layout(location = 1) in vec4 in_color;
layout(location = 0) out vec4 color;
layout(push_constant) uniform Screen { vec2 pixels; } screen;
void main() {
    vec2 ndc = vec2(in_position.x / screen.pixels.x * 2.0 - 1.0,
                    1.0 - in_position.y / screen.pixels.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    color = in_color;
}
