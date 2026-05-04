#pragma once

namespace vrp::shaders {

// VR180 hemisphere shader pair. Uses OVR_multiview2 so a single draw renders
// both eyes; the SBS UV split is done in the fragment shader using
// gl_ViewID_OVR.
inline constexpr const char* kVideoVert = R"glsl(#version 320 es
#extension GL_OVR_multiview2 : require
layout(num_views = 2) in;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;

uniform mat4 u_view[2];
uniform mat4 u_proj[2];
uniform mat4 u_model;

out vec2 v_uv;

void main() {
    gl_Position = u_proj[gl_ViewID_OVR] * u_view[gl_ViewID_OVR] * u_model * vec4(a_position, 1.0);
    v_uv = a_uv;
}
)glsl";

// Stereo modes match Kotlin NativeBridge.StereoMode:
//   0 = mono (sample full frame)
//   1 = SBS left-right (left eye = left half, right eye = right half)
inline constexpr const char* kVideoFrag = R"glsl(#version 320 es
#extension GL_OVR_multiview2 : require
#extension GL_OES_EGL_image_external_essl3 : require

precision mediump float;
in highp vec2 v_uv;

uniform samplerExternalOES u_videoTex;
uniform int u_stereoMode;
uniform highp mat4 u_texTransform;

out vec4 fragColor;

void main() {
    vec2 uv = v_uv;
    if (u_stereoMode == 1) {
        // SBS layout: left half = left eye, right half = right eye.
        uv.x = uv.x * 0.5 + (gl_ViewID_OVR == 0u ? 0.0 : 0.5);
    }
    highp vec2 videoUv = (u_texTransform * vec4(uv, 0.0, 1.0)).xy;
    fragColor = texture(u_videoTex, videoUv);
}
)glsl";

// Control panel: same multiview layout, but renders a flat coloured quad
// optionally tinted with a progress fraction. The fragment shader draws four
// sub-regions: -15s, play/pause, +15s, progress bar.
inline constexpr const char* kPanelVert = R"glsl(#version 320 es
#extension GL_OVR_multiview2 : require
layout(num_views = 2) in;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;

uniform mat4 u_view[2];
uniform mat4 u_proj[2];
uniform mat4 u_model;

out vec2 v_uv;

void main() {
    gl_Position = u_proj[gl_ViewID_OVR] * u_view[gl_ViewID_OVR] * u_model * vec4(a_position, 1.0);
    v_uv = a_uv;
}
)glsl";

inline constexpr const char* kPanelFrag = R"glsl(#version 320 es
#extension GL_OVR_multiview2 : require

precision mediump float;
in highp vec2 v_uv;

uniform float u_progress;     // 0..1
uniform int   u_isPlaying;    // 0/1, drives play/pause icon
uniform int   u_isLoading;    // 0/1, dims background
uniform float u_pointerU;     // -1 if no hover
uniform float u_pointerV;

out vec4 fragColor;

// Sub-region layout in panel UV space (origin bottom-left):
//   minus15:  [0.00..0.15]
//   playPause:[0.15..0.30]
//   plus15:   [0.30..0.45]
//   progress: [0.50..1.00]
// Each button is full-height; progress bar is the full-height right block.

vec3 buttonBg(float u) {
    if (u < 0.45) return vec3(0.16, 0.18, 0.22);
    return vec3(0.10, 0.12, 0.16);
}

float circle(vec2 p, vec2 c, float r) {
    return smoothstep(r, r - 0.005, distance(p, c));
}

float triangleRight(vec2 p, vec2 c, float s) {
    // Pointing right.
    vec2 d = p - c;
    float a = step(-s, d.x) * step(d.x, s);
    float b = step(abs(d.y) * 1.5, s - d.x);
    return a * b;
}

float triangleLeft(vec2 p, vec2 c, float s) {
    vec2 d = p - c;
    float a = step(-s, d.x) * step(d.x, s);
    float b = step(abs(d.y) * 1.5, s + d.x);
    return a * b;
}

float pauseBars(vec2 p, vec2 c, float s) {
    vec2 d = abs(p - c);
    float bar1 = step(d.x, s * 0.35) * step(s * 0.55, d.x); // outer right
    float bar2 = step(d.x, s * 0.20) * step(0.0, d.x);      // outer left
    return clamp(bar1 + bar2, 0.0, 1.0);
}

void main() {
    vec2 uv = v_uv;

    vec3 col = buttonBg(uv.x);
    float alpha = 0.85;

    if (u_isLoading == 1) {
        col *= 0.6;
    }

    // -15s region [0..0.15]
    if (uv.x < 0.15) {
        vec2 c = vec2(0.075, 0.5);
        col = mix(col, vec3(0.95), triangleLeft((uv - c) * 4.0, vec2(0.0), 0.25) * 0.9);
        col = mix(col, vec3(0.95), triangleLeft((uv - vec2(c.x + 0.02, c.y)) * 4.0, vec2(0.0), 0.25) * 0.9);
    }
    // play/pause region [0.15..0.30]
    else if (uv.x < 0.30) {
        vec2 c = vec2(0.225, 0.5);
        if (u_isPlaying == 1) {
            col = mix(col, vec3(0.95), pauseBars((uv - c) * 4.0, vec2(0.0), 0.4));
        } else {
            col = mix(col, vec3(0.95), triangleRight((uv - c) * 4.0, vec2(0.0), 0.30) * 0.9);
        }
    }
    // +15s region [0.30..0.45]
    else if (uv.x < 0.45) {
        vec2 c = vec2(0.375, 0.5);
        col = mix(col, vec3(0.95), triangleRight((uv - c) * 4.0, vec2(0.0), 0.25) * 0.9);
        col = mix(col, vec3(0.95), triangleRight((uv - vec2(c.x - 0.02, c.y)) * 4.0, vec2(0.0), 0.25) * 0.9);
    }
    // gap 0.45..0.50
    else if (uv.x < 0.50) {
        col = vec3(0.0);
        alpha = 0.0;
    }
    // progress [0.50..1.00]
    else {
        float t = (uv.x - 0.50) / 0.50;
        // Track
        col = vec3(0.20, 0.22, 0.26);
        // Filled portion
        if (t <= u_progress) {
            col = vec3(0.36, 0.62, 0.96);
        }
        // Center thin band to make it feel like a slider
        float band = step(0.42, uv.y) * step(uv.y, 0.58);
        if (band < 0.5) col = mix(col, vec3(0.10, 0.12, 0.16), 1.0 - band);
    }

    // Pointer dot
    if (u_pointerU >= 0.0 && u_pointerV >= 0.0) {
        float d = distance(uv, vec2(u_pointerU, u_pointerV));
        col = mix(col, vec3(1.0, 1.0, 0.6), smoothstep(0.022, 0.018, d));
    }

    fragColor = vec4(col, alpha);
}
)glsl";

inline constexpr const char* kPointerVert = R"glsl(#version 320 es
#extension GL_OVR_multiview2 : require
layout(num_views = 2) in;

layout(location = 0) in vec3 a_position;

uniform mat4 u_view[2];
uniform mat4 u_proj[2];

void main() {
    gl_Position = u_proj[gl_ViewID_OVR] * u_view[gl_ViewID_OVR] * vec4(a_position, 1.0);
}
)glsl";

inline constexpr const char* kPointerFrag = R"glsl(#version 320 es
#extension GL_OVR_multiview2 : require

precision mediump float;

uniform vec4 u_color;

out vec4 fragColor;

void main() {
    fragColor = u_color;
}
)glsl";

}  // namespace vrp::shaders
