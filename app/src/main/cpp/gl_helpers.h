#pragma once

#include <GLES3/gl32.h>
#include <android/log.h>
#include <string>
#include <vector>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "vrplayer", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  "vrplayer", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "vrplayer", __VA_ARGS__)

namespace vrp::gl {

// Compile and link a vertex/fragment shader pair. Returns 0 on failure.
GLuint LinkProgram(const char* vert, const char* frag);

// Cheap glGetError dump. Pass a label so it's clear in logs.
void CheckError(const char* label);

// 4x4 column-major helpers (matches GL convention).
struct Mat4 {
    float m[16];
};

Mat4 MakeIdentity();
Mat4 MakeTranslation(float x, float y, float z);
Mat4 MakeScale(float x, float y, float z);
Mat4 MakeRotationY(float radians);
Mat4 Multiply(const Mat4& a, const Mat4& b);

// XR FOV → projection (depth in [0,1]).
Mat4 ProjectionFromXrFov(float angleLeft, float angleRight, float angleUp, float angleDown,
                         float near, float far);

// Build a view matrix from XrPosef (quaternion + translation).
Mat4 ViewFromPose(float qx, float qy, float qz, float qw,
                  float px, float py, float pz);

}  // namespace vrp::gl
