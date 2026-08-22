#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

// x = 1/screenWidth, y = 1/screenHeight (Renderer::Swapchain.Width/Height) - the only "camera"
// this pass needs, since it draws in raw screen-space pixels, not world space.
layout(push_constant) uniform TextPushConstants {
    vec2 invScreenSize;
} pc;

void main() {
    // stb_easy_font outputs pixel coordinates with x increasing right, y increasing down, origin
    // top-left - Vulkan's NDC is already y-down, so this maps directly with no axis flip.
    vec2 ndc = inPosition.xy * pc.invScreenSize * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    outColor = inColor;
}
