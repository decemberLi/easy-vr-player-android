#include "multiview_pipeline.h"

#include <EGL/egl.h>

#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif

namespace vrp {

namespace {
typedef void (GL_APIENTRYP PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC)(
    GLenum target, GLenum attachment, GLuint texture, GLint level,
    GLint baseViewIndex, GLsizei numViews);

PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC pglFramebufferTextureMultiviewOVR = nullptr;

void EnsureLoaded() {
    if (pglFramebufferTextureMultiviewOVR) return;
    pglFramebufferTextureMultiviewOVR = reinterpret_cast<PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC>(
        eglGetProcAddress("glFramebufferTextureMultiviewOVR"));
    if (!pglFramebufferTextureMultiviewOVR) {
        LOGE("glFramebufferTextureMultiviewOVR not available — multiview rendering will fail");
    }
}
}  // namespace

bool MultiviewFramebuffer::Create(int width, int height) {
    Destroy();
    EnsureLoaded();
    width_ = width;
    height_ = height;

    glGenTextures(1, &depthTex_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, depthTex_);
    glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_DEPTH_COMPONENT24, width, height, 2);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    glGenFramebuffers(1, &fbo_);
    return fbo_ != 0;
}

void MultiviewFramebuffer::Destroy() {
    if (fbo_)      { glDeleteFramebuffers(1, &fbo_); fbo_ = 0; }
    if (depthTex_) { glDeleteTextures(1, &depthTex_); depthTex_ = 0; }
    width_ = height_ = 0;
}

bool MultiviewFramebuffer::Bind(GLuint colorTextureArray) {
    if (!fbo_ || !pglFramebufferTextureMultiviewOVR) return false;

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    pglFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                       colorTextureArray, 0, 0, 2);
    pglFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                       depthTex_, 0, 0, 2);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LOGE("multiview FBO incomplete: 0x%x", status);
        return false;
    }
    glViewport(0, 0, width_, height_);
    return true;
}

}  // namespace vrp
