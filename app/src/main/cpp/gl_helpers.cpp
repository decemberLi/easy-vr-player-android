#include "gl_helpers.h"

#include <cmath>
#include <cstring>

namespace vrp::gl {

namespace {
GLuint CompileStage(GLenum stage, const char* src) {
    GLuint sh = glCreateShader(stage);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint status = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &status);
    if (!status) {
        GLint len = 0;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len + 1);
        glGetShaderInfoLog(sh, len, nullptr, log.data());
        LOGE("shader compile (stage=%u) failed: %s\nsource:\n%s", stage, log.data(), src);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}
}  // namespace

GLuint LinkProgram(const char* vert, const char* frag) {
    GLuint v = CompileStage(GL_VERTEX_SHADER, vert);
    if (!v) return 0;
    GLuint f = CompileStage(GL_FRAGMENT_SHADER, frag);
    if (!f) { glDeleteShader(v); return 0; }
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint status = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &status);
    if (!status) {
        GLint len = 0;
        glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len + 1);
        glGetProgramInfoLog(p, len, nullptr, log.data());
        LOGE("program link failed: %s", log.data());
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

void CheckError(const char* label) {
    GLenum e = glGetError();
    while (e != GL_NO_ERROR) {
        LOGW("GL error at %s: 0x%x", label, e);
        e = glGetError();
    }
}

Mat4 MakeIdentity() {
    Mat4 r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

Mat4 MakeTranslation(float x, float y, float z) {
    Mat4 r = MakeIdentity();
    r.m[12] = x; r.m[13] = y; r.m[14] = z;
    return r;
}

Mat4 MakeScale(float x, float y, float z) {
    Mat4 r{};
    r.m[0] = x; r.m[5] = y; r.m[10] = z; r.m[15] = 1.0f;
    return r;
}

Mat4 MakeRotationY(float radians) {
    Mat4 r = MakeIdentity();
    float c = std::cos(radians), s = std::sin(radians);
    r.m[0] =  c;  r.m[2] = s;
    r.m[8] = -s;  r.m[10] = c;
    return r;
}

Mat4 Multiply(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
            r.m[col * 4 + row] = sum;
        }
    }
    return r;
}

Mat4 ProjectionFromXrFov(float angleLeft, float angleRight, float angleUp, float angleDown,
                         float nearZ, float farZ) {
    // Standard "off-axis" perspective projection from per-eye angles.
    // Matches OpenXR's XrFovf semantics: angles are signed from the optical axis.
    float tanLeft  = std::tan(angleLeft);
    float tanRight = std::tan(angleRight);
    float tanUp    = std::tan(angleUp);
    float tanDown  = std::tan(angleDown);

    float tanW = tanRight - tanLeft;
    float tanH = tanUp - tanDown;

    Mat4 r{};
    r.m[0] = 2.0f / tanW;
    r.m[5] = 2.0f / tanH;
    r.m[8] = (tanRight + tanLeft) / tanW;
    r.m[9] = (tanUp + tanDown) / tanH;
    r.m[10] = -(farZ + nearZ) / (farZ - nearZ);
    r.m[11] = -1.0f;
    r.m[14] = -2.0f * (farZ * nearZ) / (farZ - nearZ);
    r.m[15] = 0.0f;
    return r;
}

Mat4 ViewFromPose(float qx, float qy, float qz, float qw,
                  float px, float py, float pz) {
    // Build the 3x3 rotation from the quaternion, then invert & translate.
    float xx = qx * qx, yy = qy * qy, zz = qz * qz;
    float xy = qx * qy, xz = qx * qz, yz = qy * qz;
    float wx = qw * qx, wy = qw * qy, wz = qw * qz;

    Mat4 m = MakeIdentity();
    m.m[0]  = 1.0f - 2.0f * (yy + zz);
    m.m[1]  = 2.0f * (xy + wz);
    m.m[2]  = 2.0f * (xz - wy);

    m.m[4]  = 2.0f * (xy - wz);
    m.m[5]  = 1.0f - 2.0f * (xx + zz);
    m.m[6]  = 2.0f * (yz + wx);

    m.m[8]  = 2.0f * (xz + wy);
    m.m[9]  = 2.0f * (yz - wx);
    m.m[10] = 1.0f - 2.0f * (xx + yy);

    m.m[12] = px; m.m[13] = py; m.m[14] = pz;

    // Invert: transpose 3x3, transform translation.
    Mat4 inv = MakeIdentity();
    inv.m[0] = m.m[0]; inv.m[1] = m.m[4]; inv.m[2] = m.m[8];
    inv.m[4] = m.m[1]; inv.m[5] = m.m[5]; inv.m[6] = m.m[9];
    inv.m[8] = m.m[2]; inv.m[9] = m.m[6]; inv.m[10] = m.m[10];

    inv.m[12] = -(m.m[0] * px + m.m[1] * py + m.m[2] * pz);
    inv.m[13] = -(m.m[4] * px + m.m[5] * py + m.m[6] * pz);
    inv.m[14] = -(m.m[8] * px + m.m[9] * py + m.m[10] * pz);
    return inv;
}

}  // namespace vrp::gl
