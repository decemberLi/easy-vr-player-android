#pragma once

#include "gl_helpers.h"
#include <vector>

namespace vrp {

/**
 * Per-eye FBO that wraps an OpenXR swapchain image (GL_TEXTURE_2D_ARRAY with
 * 2 layers — one per eye) and a matching depth array.
 *
 * The single FBO is bound once per frame; the multiview shader scatters output
 * to the right layer based on gl_ViewID_OVR. We re-attach the colour layer
 * each frame because the swapchain rotates through several images.
 */
class MultiviewFramebuffer {
public:
    MultiviewFramebuffer() = default;
    ~MultiviewFramebuffer() { Destroy(); }

    bool Create(int width, int height);
    void Destroy();

    bool Bind(GLuint colorTextureArray);

    int Width()  const { return width_; }
    int Height() const { return height_; }

private:
    int width_ = 0;
    int height_ = 0;
    GLuint fbo_ = 0;
    GLuint depthTex_ = 0;
};

}  // namespace vrp
