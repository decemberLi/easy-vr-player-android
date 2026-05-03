#include "vr180_mesh.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace vrp {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

void VrSphereMesh::Create(float radius, float horizontalFovDeg, float verticalFovDeg) {
    Destroy();

    float clipHFov = std::clamp(horizontalFovDeg, 0.0f, 360.0f);
    float clipVFov = std::clamp(verticalFovDeg,  0.0f, 180.0f);
    int verticalSlices   = std::max(1, int(clipHFov / 3.0f)); // ~3° per slice → 60 around
    int horizontalSlices = std::max(1, int(clipVFov / 3.0f));

    float verticalScale  = clipVFov / 180.0f;
    float verticalOffset = (1.0f - verticalScale) / 2.0f;
    float horizontalScale  = clipHFov / 360.0f;
    float horizontalOffset = (1.0f - horizontalScale) / 2.0f;

    // The source UV is the full frame (0..1) — at VR180 we cover the whole
    // source. Same scale logic as iOS, kept verbatim for clarity.
    float uvHScale  = clipHFov / clipHFov; // == 1, retained for symmetry
    float uvHOffset = 0.0f;
    float uvVScale  = clipVFov / clipVFov;
    float uvVOffset = 0.0f;

    int rows = horizontalSlices + 1;
    int cols = verticalSlices + 1;
    int vertCount = rows * cols;

    struct V { float x, y, z, u, v; };
    std::vector<V> verts(vertCount);
    std::vector<uint32_t> indices;
    indices.reserve(verticalSlices * horizontalSlices * 6);

    for (int y = 0; y < rows; ++y) {
        float angle1 =
            (kPi * float(y) / float(horizontalSlices)) * verticalScale + (verticalOffset * kPi);
        float sin1 = std::sin(angle1);
        float cos1 = std::cos(angle1);
        for (int x = 0; x < cols; ++x) {
            float angle2 =
                (kPi * 2.0f * float(x) / float(verticalSlices)) * horizontalScale +
                (horizontalOffset * kPi * 2.0f);
            float sin2 = std::sin(angle2);
            float cos2 = std::cos(angle2);

            V& vert = verts[size_t(x + y * cols)];
            vert.x = sin1 * cos2 * radius;
            vert.y = cos1 * radius;
            vert.z = sin1 * sin2 * radius;

            float u = float(x) / float(verticalSlices);
            float v = 1.0f - float(y) / float(horizontalSlices);
            vert.u = u * uvHScale + uvHOffset;
            vert.v = v * uvVScale + uvVOffset;
        }
    }

    for (int y = 0; y < horizontalSlices; ++y) {
        for (int x = 0; x < verticalSlices; ++x) {
            uint32_t current = uint32_t(x + y * cols);
            uint32_t next = current + uint32_t(cols);
            indices.push_back(current + 1);
            indices.push_back(current);
            indices.push_back(next + 1);
            indices.push_back(next + 1);
            indices.push_back(current);
            indices.push_back(next);
        }
    }

    indexCount_ = GLsizei(indices.size());

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 GLsizeiptr(verts.size() * sizeof(V)),
                 verts.data(),
                 GL_STATIC_DRAW);

    glGenBuffers(1, &ebo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 GLsizeiptr(indices.size() * sizeof(uint32_t)),
                 indices.data(),
                 GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(V), reinterpret_cast<void*>(offsetof(V, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(V), reinterpret_cast<void*>(offsetof(V, u)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    LOGI("VrSphereMesh built verts=%d indices=%d radius=%.2f hFov=%.1f vFov=%.1f",
         vertCount, indexCount_, radius, clipHFov, clipVFov);
}

void VrSphereMesh::Destroy() {
    if (ebo_) { glDeleteBuffers(1, &ebo_); ebo_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    indexCount_ = 0;
}

void VrSphereMesh::Draw() const {
    if (!vao_) return;
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

gl::Mat4 VrSphereMesh::ModelTransform() const {
    // iOS rotates the mesh -π/2 around Y to align the hemisphere with the
    // forward (-Z) direction. We do the same here.
    return gl::MakeRotationY(-kPi * 0.5f);
}

}  // namespace vrp
