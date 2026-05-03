#include "video_texture.h"

namespace vrp {

void VideoTexture::CreateTexture() {
    if (texId_) return;
    glGenTextures(1, &texId_);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, texId_);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
    LOGI("VideoTexture created OES texId=%u", texId_);
}

void VideoTexture::Destroy() {
    if (texId_) {
        glDeleteTextures(1, &texId_);
        texId_ = 0;
    }
    attached_ = false;
    frameAvailable_.store(false, std::memory_order_release);
    // surfaceTextureGlobal_ is released by the JNI bridge.
}

void VideoTexture::CacheMethodIds(JNIEnv* env) {
    if (midAttach_ && midUpdate_ && midDetach_) return;
    jclass cls = env->GetObjectClass(surfaceTextureGlobal_);
    midAttach_ = env->GetMethodID(cls, "attachToGLContext", "(I)V");
    midDetach_ = env->GetMethodID(cls, "detachFromGLContext", "()V");
    midUpdate_ = env->GetMethodID(cls, "updateTexImage", "()V");
    env->DeleteLocalRef(cls);
    if (!midAttach_ || !midUpdate_) {
        LOGE("VideoTexture failed to look up SurfaceTexture method ids");
    }
}

void VideoTexture::SetSurfaceTexture(JNIEnv* env, jobject surfaceTexture) {
    if (surfaceTextureGlobal_) {
        // Best-effort detach before swapping.
        if (attached_ && midDetach_) {
            env->CallVoidMethod(surfaceTextureGlobal_, midDetach_);
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
        env->DeleteGlobalRef(surfaceTextureGlobal_);
    }
    surfaceTextureGlobal_ = surfaceTexture ? env->NewGlobalRef(surfaceTexture) : nullptr;
    midAttach_ = midDetach_ = midUpdate_ = nullptr;
    attached_ = false;
    if (surfaceTextureGlobal_) CacheMethodIds(env);
}

void VideoTexture::ClearSurfaceTexture(JNIEnv* env) {
    if (!surfaceTextureGlobal_) return;
    if (attached_ && midDetach_) {
        env->CallVoidMethod(surfaceTextureGlobal_, midDetach_);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    env->DeleteGlobalRef(surfaceTextureGlobal_);
    surfaceTextureGlobal_ = nullptr;
    midAttach_ = midDetach_ = midUpdate_ = nullptr;
    attached_ = false;
}

bool VideoTexture::AttachToGl(JNIEnv* env) {
    if (attached_) return true;
    if (!surfaceTextureGlobal_ || !midAttach_ || !texId_) return false;

    // The Java-side SurfaceTexture was created with an auto-generated texture.
    // Detach it first so we can re-attach to the texture native created.
    if (midDetach_) {
        env->CallVoidMethod(surfaceTextureGlobal_, midDetach_);
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
    }

    env->CallVoidMethod(surfaceTextureGlobal_, midAttach_, jint(texId_));
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        return false;
    }
    attached_ = true;
    LOGI("VideoTexture attached SurfaceTexture to texId=%u", texId_);
    return true;
}

bool VideoTexture::UpdateIfNeeded(JNIEnv* env) {
    if (!surfaceTextureGlobal_ || !midUpdate_) return false;
    if (!frameAvailable_.exchange(false, std::memory_order_acq_rel)) return false;
    env->CallVoidMethod(surfaceTextureGlobal_, midUpdate_);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        return false;
    }
    return true;
}

}  // namespace vrp
