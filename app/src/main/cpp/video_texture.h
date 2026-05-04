#pragma once

#include <atomic>
#include <jni.h>
#include "gl_helpers.h"

#define GL_TEXTURE_EXTERNAL_OES 0x8D65

namespace vrp {

/**
 * Bridges a Java-side SurfaceTexture to a GL OES texture that the renderer
 * samples in the VR180 fragment shader.
 *
 * Lifecycle (all GL ops on the render thread unless noted):
 *   1. CreateTexture()                      — Generates the OES texture name.
 *   2. (JVM)  bindVideoSurfaceTexture(...)  — Hands us a Java SurfaceTexture.
 *   3. AttachToGl(env)                      — Calls SurfaceTexture.attachToGLContext.
 *   4. PostFrameAvailable() (any thread)    — Flag flip.
 *   5. UpdateIfNeeded(env)                  — Checks the flag and calls updateTexImage.
 */
class VideoTexture {
public:
    VideoTexture() = default;
    ~VideoTexture() { Destroy(); }

    void CreateTexture();
    void Destroy();

    GLuint TextureId() const { return texId_; }
    const float* TransformMatrix() const { return transform_; }

    /** Save a global ref to the SurfaceTexture object provided by JVM. */
    void SetSurfaceTexture(JNIEnv* env, jobject surfaceTexture);
    void ClearSurfaceTexture(JNIEnv* env);

    /** Attach the texture to the current GL context. Safe to call multiple times. */
    bool AttachToGl(JNIEnv* env);

    /** Flag a new frame ready. Called from any thread. */
    void PostFrameAvailable() { frameAvailable_.store(true, std::memory_order_release); }

    /** If a frame was posted, call SurfaceTexture.updateTexImage() and clear flag. */
    bool UpdateIfNeeded(JNIEnv* env);

private:
    GLuint texId_ = 0;
    bool attached_ = false;
    std::atomic<bool> frameAvailable_{false};
    jobject surfaceTextureGlobal_ = nullptr;

    // Cached method IDs.
    jmethodID midAttach_ = nullptr;
    jmethodID midDetach_ = nullptr;
    jmethodID midUpdate_ = nullptr;
    jmethodID midTransform_ = nullptr;
    float transform_[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };

    void CacheMethodIds(JNIEnv* env);
};

}  // namespace vrp
