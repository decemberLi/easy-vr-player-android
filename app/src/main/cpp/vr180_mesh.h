#pragma once

#include "gl_helpers.h"

namespace vrp {

/**
 * GPU-resident hemisphere/sphere mesh for VR180 (or VR360, when clipFov >= 360).
 *
 * Direct port of [VideoTools.generateVideoSphere] in the iOS reference. Vertices
 * are wound so the visible side is the inside (normals point inwards). UVs are
 * laid out so the full source frame maps to the chosen field of view; SBS
 * left/right splitting happens later in the fragment shader.
 */
class VrSphereMesh {
public:
    VrSphereMesh() = default;
    ~VrSphereMesh() { Destroy(); }

    VrSphereMesh(const VrSphereMesh&) = delete;
    VrSphereMesh& operator=(const VrSphereMesh&) = delete;

    /**
     * Create the GL buffers. Call from the GL thread once.
     *
     *   radius           in meters; the sphere is rendered around the head.
     *   horizontalFovDeg of the source content (180 for VR180).
     *   verticalFovDeg   of the source content (180 for VR180).
     */
    void Create(float radius = 2.0f, float horizontalFovDeg = 180.0f, float verticalFovDeg = 180.0f);
    void Destroy();

    /** Bind VAO + VBO + EBO and draw the indexed mesh. */
    void Draw() const;

    /** Model transform applied per draw — same as iOS makeVideoMesh's rotation. */
    gl::Mat4 ModelTransform() const;

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    GLsizei indexCount_ = 0;
};

}  // namespace vrp
