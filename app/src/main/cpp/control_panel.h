#pragma once

#include "gl_helpers.h"

namespace vrp {

/**
 * Floating control panel quad rendered in the immersive scene.
 *
 * Layout in panel UV space (origin bottom-left):
 *   x [0.00..0.15] = -15s
 *   x [0.15..0.30] = play/pause
 *   x [0.30..0.45] = +15s
 *   x [0.50..1.00] = scrubber
 */
class ControlPanel {
public:
    enum HitRegion {
        Region_None = 0,
        Region_Minus15,
        Region_PlayPause,
        Region_Plus15,
        Region_Scrubber,
    };

    struct HitResult {
        HitRegion region = Region_None;
        float u = -1.0f;        // panel UV x
        float v = -1.0f;        // panel UV y
        float scrubFraction = 0.0f;  // valid when region == Region_Scrubber
    };

    void Create();
    void Destroy();

    void Draw(GLuint program, const gl::Mat4& view0, const gl::Mat4& view1,
              const gl::Mat4& proj0, const gl::Mat4& proj1,
              const HitResult& hover);

    /** Width / height of the panel in meters. */
    float Width()  const { return width_;  }
    float Height() const { return height_; }

    /** World-space transform applied to the quad. */
    gl::Mat4 ModelTransform() const;

    /**
     * Intersect a ray (origin + dir) with the panel plane and resolve into a
     * region. Returns Region_None if the ray misses the plane.
     */
    HitResult HitTestRay(float ox, float oy, float oz,
                         float dx, float dy, float dz) const;

    /** Convenience: hit-test using head pose forward as the gaze ray. */
    HitResult HitTestGaze(const gl::Mat4& headView) const;

    void SetIsPlaying(bool playing) { isPlaying_ = playing; }
    void SetIsLoading(bool loading) { isLoading_ = loading; }
    void SetProgress(float fraction) { progress_ = fraction; }

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    GLsizei indexCount_ = 0;

    float width_  = 1.6f;
    float height_ = 0.4f;
    float distance_ = 1.5f;     // forward (-Z)
    float verticalOffset_ = -0.35f; // below eye line

    bool  isPlaying_ = false;
    bool  isLoading_ = false;
    float progress_ = 0.0f;
};

}  // namespace vrp
