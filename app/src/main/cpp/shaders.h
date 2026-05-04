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

// Control panel: same multiview layout, rendered as a compact translucent
// transport surface with shader-drawn controls and seven-segment time text.
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
uniform int   u_currentSeconds;
uniform int   u_durationSeconds;

out vec4 fragColor;

const float kPanelAspect = 4.0;

vec2 panelSpace(vec2 uv) {
    return vec2(uv.x * kPanelAspect, uv.y);
}

float sdRoundRect(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + vec2(r);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float fillRoundRect(vec2 uv, vec2 center, vec2 halfSize, float radius, float feather) {
    return 1.0 - smoothstep(0.0, feather, sdRoundRect(uv - center, halfSize, radius));
}

float strokeRoundRect(vec2 uv, vec2 center, vec2 halfSize, float radius, float width, float feather) {
    float d = sdRoundRect(uv - center, halfSize, radius);
    return 1.0 - smoothstep(width, width + feather, abs(d));
}

float lineSegment(vec2 uv, vec2 a, vec2 b, float width) {
    vec2 pa = uv - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return 1.0 - smoothstep(width, width + 0.006, length(pa - ba * h));
}

float triangleRight(vec2 p, vec2 c, float s) {
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

float rectFill(vec2 uv, vec2 center, vec2 halfSize) {
    vec2 d = abs(uv - center);
    return step(d.x, halfSize.x) * step(d.y, halfSize.y);
}

float digitSegments(int digit, int segment) {
    if (digit == 0) return (segment == 6) ? 0.0 : 1.0;
    if (digit == 1) return (segment == 1 || segment == 2) ? 1.0 : 0.0;
    if (digit == 2) return (segment == 0 || segment == 1 || segment == 3 || segment == 4 || segment == 6) ? 1.0 : 0.0;
    if (digit == 3) return (segment == 0 || segment == 1 || segment == 2 || segment == 3 || segment == 6) ? 1.0 : 0.0;
    if (digit == 4) return (segment == 1 || segment == 2 || segment == 5 || segment == 6) ? 1.0 : 0.0;
    if (digit == 5) return (segment == 0 || segment == 2 || segment == 3 || segment == 5 || segment == 6) ? 1.0 : 0.0;
    if (digit == 6) return (segment == 0 || segment == 2 || segment == 3 || segment == 4 || segment == 5 || segment == 6) ? 1.0 : 0.0;
    if (digit == 7) return (segment == 0 || segment == 1 || segment == 2) ? 1.0 : 0.0;
    if (digit == 8) return 1.0;
    if (digit == 9) return (segment == 0 || segment == 1 || segment == 2 || segment == 3 || segment == 5 || segment == 6) ? 1.0 : 0.0;
    return 0.0;
}

float sevenDigit(vec2 uv, vec2 origin, float scale, int digit) {
    vec2 p = (uv - origin) / scale;
    float on = 0.0;
    on = max(on, digitSegments(digit, 0) * lineSegment(p, vec2(0.18, 0.88), vec2(0.82, 0.88), 0.055));
    on = max(on, digitSegments(digit, 1) * lineSegment(p, vec2(0.86, 0.82), vec2(0.86, 0.55), 0.055));
    on = max(on, digitSegments(digit, 2) * lineSegment(p, vec2(0.86, 0.45), vec2(0.86, 0.18), 0.055));
    on = max(on, digitSegments(digit, 3) * lineSegment(p, vec2(0.18, 0.12), vec2(0.82, 0.12), 0.055));
    on = max(on, digitSegments(digit, 4) * lineSegment(p, vec2(0.14, 0.45), vec2(0.14, 0.18), 0.055));
    on = max(on, digitSegments(digit, 5) * lineSegment(p, vec2(0.14, 0.82), vec2(0.14, 0.55), 0.055));
    on = max(on, digitSegments(digit, 6) * lineSegment(p, vec2(0.20, 0.50), vec2(0.80, 0.50), 0.055));
    return on;
}

float colon(vec2 uv, vec2 origin, float scale) {
    vec2 p = (uv - origin) / scale;
    float top = 1.0 - smoothstep(0.060, 0.075, distance(p, vec2(0.50, 0.64)));
    float bot = 1.0 - smoothstep(0.060, 0.075, distance(p, vec2(0.50, 0.36)));
    return max(top, bot);
}

float slashGlyph(vec2 uv, vec2 origin, float scale) {
    vec2 p = (uv - origin) / scale;
    return lineSegment(p, vec2(0.24, 0.12), vec2(0.76, 0.88), 0.045);
}

float timeGlyph(vec2 uv, vec2 origin, float scale, int seconds) {
    int clamped = min(max(seconds, 0), 5999);
    int minutes = clamped / 60;
    int secs = clamped - minutes * 60;
    int m0 = (minutes / 10) - (minutes / 100) * 10;
    int m1 = minutes - (minutes / 10) * 10;
    int s0 = secs / 10;
    int s1 = secs - s0 * 10;

    float x = 0.0;
    float on = 0.0;
    on = max(on, sevenDigit(uv, origin + vec2(x, 0.0), scale, m0)); x += scale * 0.84;
    on = max(on, sevenDigit(uv, origin + vec2(x, 0.0), scale, m1)); x += scale * 0.74;
    on = max(on, colon(uv, origin + vec2(x, 0.0), scale * 0.86)); x += scale * 0.54;
    on = max(on, sevenDigit(uv, origin + vec2(x, 0.0), scale, s0)); x += scale * 0.84;
    on = max(on, sevenDigit(uv, origin + vec2(x, 0.0), scale, s1));
    return on;
}

void main() {
    vec2 uv = v_uv;
    vec2 sp = panelSpace(uv);
    vec2 pointerUv = vec2(u_pointerU, u_pointerV);
    vec2 pointerSp = panelSpace(pointerUv);

    float panel = fillRoundRect(sp, vec2(2.0, 0.5), vec2(1.968, 0.462), 0.060, 0.012);
    if (panel < 0.01) discard;

    vec3 col = mix(vec3(0.055, 0.064, 0.078), vec3(0.13, 0.15, 0.17), uv.y);
    col += vec3(0.030, 0.042, 0.052) * (1.0 - length((sp - vec2(2.0, 0.5)) / vec2(2.0, 0.5)));
    float alpha = 0.90 * panel;

    float rim = strokeRoundRect(sp, vec2(2.0, 0.5), vec2(1.968, 0.462), 0.060, 0.010, 0.010);
    col = mix(col, vec3(0.48, 0.58, 0.64), rim * 0.30);

    float topSheen = smoothstep(0.40, 0.98, uv.y) * (1.0 - smoothstep(0.00, 0.020, abs(uv.y - 0.91)));
    col += vec3(0.10, 0.12, 0.13) * topSheen;

    if (u_isLoading == 1) {
        col *= 0.72;
    }

    vec3 accent = vec3(0.20, 0.78, 0.92);
    vec3 warm = vec3(1.00, 0.76, 0.28);
    vec3 text = vec3(0.88, 0.93, 0.96);

    vec2 centers[3];
    centers[0] = vec2(0.360, 0.560);
    centers[1] = vec2(0.860, 0.560);
    centers[2] = vec2(1.360, 0.560);
    for (int i = 0; i < 3; ++i) {
        float hover = fillRoundRect(pointerSp, centers[i], vec2(0.208, 0.250), 0.036, 0.002);
        float body = fillRoundRect(sp, centers[i], vec2(0.208, 0.250), 0.036, 0.010);
        float stroke = strokeRoundRect(sp, centers[i], vec2(0.208, 0.250), 0.036, 0.006, 0.007);
        vec3 bg = mix(vec3(0.13, 0.15, 0.17), vec3(0.21, 0.26, 0.28), hover);
        col = mix(col, bg, body * 0.92);
        col = mix(col, hover > 0.0 ? warm : vec3(0.36, 0.42, 0.46), stroke * 0.50);
    }

    float icon = 0.0;
    icon = max(icon, triangleLeft((sp - vec2(0.320, 0.560)) * 10.0, vec2(0.0), 0.33));
    icon = max(icon, triangleLeft((sp - vec2(0.404, 0.560)) * 10.0, vec2(0.0), 0.33));

    if (u_isPlaying == 1) {
        icon = max(icon, rectFill(sp, vec2(0.816, 0.560), vec2(0.040, 0.080)));
        icon = max(icon, rectFill(sp, vec2(0.904, 0.560), vec2(0.040, 0.080)));
    } else {
        icon = max(icon, triangleRight((sp - vec2(0.860, 0.560)) * 9.0, vec2(0.0), 0.34));
    }
    icon = max(icon, triangleRight((sp - vec2(1.320, 0.560)) * 10.0, vec2(0.0), 0.33));
    icon = max(icon, triangleRight((sp - vec2(1.404, 0.560)) * 10.0, vec2(0.0), 0.33));
    col = mix(col, text, icon);

    float track = fillRoundRect(sp, vec2(2.820, 0.530), vec2(1.020, 0.036), 0.020, 0.006);
    col = mix(col, vec3(0.070, 0.085, 0.095), track * 0.92);
    float t = clamp((sp.x - 1.800) / 2.040, 0.0, 1.0);
    float filled = track * step(t, u_progress);
    col = mix(col, accent, filled * 0.95);

    vec2 knobCenter = vec2(1.800 + clamp(u_progress, 0.0, 1.0) * 2.040, 0.530);
    float knobShadow = 1.0 - smoothstep(0.040, 0.058, distance(sp, knobCenter + vec2(0.024, -0.008)));
    float knob = 1.0 - smoothstep(0.028, 0.036, distance(sp, knobCenter));
    col = mix(col, vec3(0.0), knobShadow * 0.18);
    col = mix(col, vec3(0.95, 0.99, 1.0), knob);
    col = mix(col, accent, (1.0 - smoothstep(0.010, 0.016, distance(sp, knobCenter))) * 0.75);

    float timeCurrent = timeGlyph(sp, vec2(1.800, 0.180), 0.032, u_currentSeconds);
    float slash = slashGlyph(sp, vec2(2.460, 0.180), 0.032);
    int durationSeconds = u_durationSeconds > 0 ? u_durationSeconds : 0;
    float timeDuration = timeGlyph(sp, vec2(2.600, 0.180), 0.032, durationSeconds);
    float timeMask = max(max(timeCurrent, slash * 0.75), timeDuration * 0.82);
    col = mix(col, vec3(0.80, 0.88, 0.92), timeMask);

    float marker = 0.0;
    for (int i = 1; i < 4; ++i) {
        float mx = 1.800 + 2.040 * float(i) * 0.25;
        marker = max(marker, lineSegment(sp, vec2(mx, 0.472), vec2(mx, 0.492), 0.0025));
    }
    col = mix(col, vec3(0.62, 0.70, 0.75), marker * 0.30);

    if (u_pointerU >= 0.0 && u_pointerV >= 0.0) {
        float halo = 1.0 - smoothstep(0.050, 0.090, distance(sp, pointerSp));
        float dot = 1.0 - smoothstep(0.014, 0.024, distance(sp, pointerSp));
        col = mix(col, warm, halo * 0.25);
        col = mix(col, vec3(1.0, 0.95, 0.58), dot);
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
