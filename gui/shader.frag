#version 450
layout(location = 0) in vec4 color;
layout(location = 1) in vec2 uv;
layout(binding = 0) uniform sampler2D image_texture;
layout(location = 0) out vec4 out_color;
void main() { out_color = color * texture(image_texture, uv); }
