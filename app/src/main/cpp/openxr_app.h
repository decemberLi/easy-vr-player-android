#pragma once

#include <atomic>
#include <chrono>
#include <jni.h>
#include <mutex>
#include <thread>
#include <vector>

#include <EGL/egl.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "control_panel.h"
#include "gl_helpers.h"
#include "multiview_pipeline.h"
#include "video_texture.h"
#include "vr180_mesh.h"

namespace vrp {

/**
 * Owns the OpenXR session and the GL render thread for a single immersive
 * activity. There is one global instance (created/destroyed by the JNI bridge).
 */
class OpenXRApp {
public:
    static OpenXRApp& Get();

    void Create(JNIEnv* env, JavaVM* jvm, jobject activity);
    void Destroy();

    void Resume();
    void Pause();

    /** Set/clear the playback callback (NativeBridge.Callback). */
    void SetCallback(JNIEnv* env, jobject callback);

    void SetStereoMode(int mode) { stereoMode_.store(mode, std::memory_order_release); }
    void SetPlaybackState(bool isPlaying, bool isLoading, long long currentMs, long long durationMs) {
        isPlaying_.store(isPlaying);
        isLoading_.store(isLoading);
        currentMs_.store(currentMs);
        durationMs_.store(durationMs);
    }

    /** Block until GL has come up and the OES texture is created. */
    GLuint AcquireVideoTextureId();

    void BindVideoSurfaceTexture(JNIEnv* env, jobject surfaceTexture);
    void PostFrameAvailable() { videoTex_.PostFrameAvailable(); }

private:
    OpenXRApp() = default;

    bool CreateXrInstance(JNIEnv* env, jobject activity);
    void RenderThreadMain();
    bool InitEgl();
    void DestroyEgl();
    bool InitXr(JNIEnv* env);
    void DestroyXr();
    bool InitGlResources();
    void DestroyGlResources();

    void HandleEvents();
    void RenderFrame(JNIEnv* env);

    void DispatchPlayPause(JNIEnv* env);
    void DispatchSeekRelative(JNIEnv* env, long long deltaMs);
    void DispatchSeekFraction(JNIEnv* env, float fraction);
    void DispatchExit(JNIEnv* env);
    bool CreatePointerResources();
    void DestroyPointerResources();

    struct ControllerRay {
        bool valid = false;
        float origin[3] = {};
        float direction[3] = {};
        ControlPanel::HitResult hover{};
    };
    bool LocateControllerRay(int hand, XrTime displayTime, ControllerRay& ray);
    void DrawControllerRay(const ControllerRay& ray,
                           const gl::Mat4& view0, const gl::Mat4& view1,
                           const gl::Mat4& proj0, const gl::Mat4& proj1,
                           const float color[4]);

private:
    JavaVM* jvm_ = nullptr;
    jobject appContextGlobal_ = nullptr;
    jobject activityGlobal_ = nullptr;
    jobject callbackGlobal_ = nullptr;
    jmethodID midPlayPause_ = nullptr;
    jmethodID midSeekRel_ = nullptr;
    jmethodID midSeekFrac_ = nullptr;
    jmethodID midExit_ = nullptr;

    // Render-thread sync.
    std::thread renderThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> sessionActive_{false};
    std::atomic<bool> resumed_{false};
    std::atomic<bool> textureReady_{false};
    std::atomic<bool> initFailed_{false};
    std::mutex pendingMutex_;
    jobject pendingSurfaceTexture_ = nullptr;
    bool needsAttach_ = false;

    // Playback state mirror.
    std::atomic<int> stereoMode_{1};
    std::atomic<bool> isPlaying_{false};
    std::atomic<bool> isLoading_{false};
    std::atomic<long long> currentMs_{0};
    std::atomic<long long> durationMs_{0};

    // EGL
    EGLDisplay eglDisplay_ = EGL_NO_DISPLAY;
    EGLContext eglContext_ = EGL_NO_CONTEXT;
    EGLSurface eglPbuffer_ = EGL_NO_SURFACE;
    EGLConfig  eglConfig_  = nullptr;

    // OpenXR
    XrInstance instance_ = XR_NULL_HANDLE;
    XrSystemId systemId_ = XR_NULL_SYSTEM_ID;
    XrSession  session_  = XR_NULL_HANDLE;
    XrSpace    localSpace_ = XR_NULL_HANDLE;
    XrSpace    aimSpace_[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
    XrSwapchain swapchain_ = XR_NULL_HANDLE;
    int swapchainWidth_ = 0;
    int swapchainHeight_ = 0;
    std::vector<XrSwapchainImageOpenGLESKHR> swapchainImages_;
    XrActionSet actionSet_ = XR_NULL_HANDLE;
    XrAction triggerAction_ = XR_NULL_HANDLE;
    XrAction aimPoseAction_ = XR_NULL_HANDLE;
    XrAction buttonAAction_ = XR_NULL_HANDLE;
    XrAction buttonBAction_ = XR_NULL_HANDLE;
    XrAction thumbstickXAction_ = XR_NULL_HANDLE;
    XrAction thumbstickYAction_ = XR_NULL_HANDLE;
    XrAction thumbstickClickAction_ = XR_NULL_HANDLE;
    XrPath subactionPaths_[2] = {};
    XrSessionState sessionState_ = XR_SESSION_STATE_UNKNOWN;
    XrViewConfigurationView viewConfigs_[2] = {};

    // GL resources
    GLuint videoProgram_ = 0;
    GLuint panelProgram_ = 0;
    GLuint pointerProgram_ = 0;
    GLuint pointerVao_ = 0;
    GLuint pointerVbo_ = 0;
    VrSphereMesh mesh_;
    ControlPanel panel_;
    VideoTexture videoTex_;
    MultiviewFramebuffer fbo_;

    // UI hover/trigger latch
    bool prevTrigger_[2] = { false, false };
    bool prevButtonA_ = false;
    bool prevButtonB_ = false;
    bool prevThumbstickClick_[2] = { false, false };
    int prevThumbstickXDir_[2] = { 0, 0 };
    int prevThumbstickYDir_[2] = { 0, 0 };
    bool panelVisible_ = true;
    std::chrono::steady_clock::time_point panelLastInteractionTime_{};
    std::chrono::steady_clock::time_point lastThumbstickSeekTime_{};
    ControlPanel::HitResult hover_{};
};

}  // namespace vrp
