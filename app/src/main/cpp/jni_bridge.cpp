#include <jni.h>
#include <android/log.h>

#include "openxr_app.h"

#define JNI_FN(name) Java_space_vrplayer_immersive_NativeBridge_##name

extern "C" {

JNIEXPORT void JNICALL JNI_FN(create)(JNIEnv* env, jobject /*thiz*/, jobject activity) {
    JavaVM* jvm = nullptr;
    env->GetJavaVM(&jvm);
    vrp::OpenXRApp::Get().Create(env, jvm, activity);
}

JNIEXPORT void JNICALL JNI_FN(destroy)(JNIEnv* /*env*/, jobject /*thiz*/) {
    vrp::OpenXRApp::Get().Destroy();
}

JNIEXPORT void JNICALL JNI_FN(resume)(JNIEnv* /*env*/, jobject /*thiz*/) {
    vrp::OpenXRApp::Get().Resume();
}

JNIEXPORT void JNICALL JNI_FN(pause)(JNIEnv* /*env*/, jobject /*thiz*/) {
    vrp::OpenXRApp::Get().Pause();
}

JNIEXPORT jint JNICALL JNI_FN(acquireVideoTextureId)(JNIEnv* /*env*/, jobject /*thiz*/) {
    return jint(vrp::OpenXRApp::Get().AcquireVideoTextureId());
}

JNIEXPORT void JNICALL JNI_FN(bindVideoSurfaceTexture)(JNIEnv* env, jobject /*thiz*/, jobject surfaceTexture) {
    vrp::OpenXRApp::Get().BindVideoSurfaceTexture(env, surfaceTexture);
}

JNIEXPORT void JNICALL JNI_FN(postFrameAvailable)(JNIEnv* /*env*/, jobject /*thiz*/) {
    vrp::OpenXRApp::Get().PostFrameAvailable();
}

JNIEXPORT void JNICALL JNI_FN(setPlaybackState)(JNIEnv* /*env*/, jobject /*thiz*/,
                                                jboolean isPlaying, jboolean isLoading,
                                                jlong currentMs, jlong durationMs) {
    vrp::OpenXRApp::Get().SetPlaybackState(isPlaying, isLoading,
                                           static_cast<long long>(currentMs),
                                           static_cast<long long>(durationMs));
}

JNIEXPORT void JNICALL JNI_FN(setStereoMode)(JNIEnv* /*env*/, jobject /*thiz*/, jint mode) {
    vrp::OpenXRApp::Get().SetStereoMode(int(mode));
}

JNIEXPORT void JNICALL JNI_FN(setCallback)(JNIEnv* env, jobject /*thiz*/, jobject callback) {
    vrp::OpenXRApp::Get().SetCallback(env, callback);
}

}  // extern "C"
