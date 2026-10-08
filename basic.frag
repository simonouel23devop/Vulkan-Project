#version 450
#include "common.glsl"

layout(location = 0) in vec2 vertex_uv;

layout(location = 0) out vec4 out_color;

layout(set = 1, binding = 0) uniform sampler2D texture_sampler;

void main() {
    out_color = texture(texture_sampler, vertex_uv);
}