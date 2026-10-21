#version 450
// Android pre-rotation (nvn_vk_draw.c rot_draw): one triangle over the portrait swapchain image; uv is where the landscape
// picture is read. q = this corner in the swapchain image (0..2, y down); cw = 1: content turned 90 degrees clockwise.
layout(push_constant) uniform P { int cw; } p;
layout(location = 0) out vec2 uv;
void main() {
    vec2 q = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(q * 2.0 - 1.0, 0.0, 1.0);
    uv = p.cw != 0 ? vec2(q.y, 1.0 - q.x) : vec2(1.0 - q.y, q.x);
}
