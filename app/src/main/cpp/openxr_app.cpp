#include "openxr_app.h"
#include "shaders.h"

#include <EGL/eglext.h>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

#define XR_OK(call)                                                            \
    do {                                                                       \
        XrResult _r = (call);                                                  \
        if (XR_FAILED(_r)) {                                                   \
            char _s[XR_MAX_RESULT_STRING_SIZE] = {0};                          \
            if (instance_ != XR_NULL_HANDLE) xrResultToString(instance_, _r, _s); \
            LOGE("OpenXR call failed (%s): %s", #call, _s[0] ? _s : "?");      \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define XR_TRY(call)                                                           \
    do {                                                                       \
        XrResult _r = (call);                                                  \
        if (XR_FAILED(_r)) {                                                   \
            char _s[XR_MAX_RESULT_STRING_SIZE] = {0};                          \
            if (instance_ != XR_NULL_HANDLE) xrResultToString(instance_, _r, _s); \
            LOGW("OpenXR call failed (%s): %s", #call, _s[0] ? _s : "?");      \
        }                                                                      \
    } while (0)

namespace vrp {

namespace {

PFN_xrInitializeLoaderKHR pfnXrInitializeLoaderKHR = nullptr;
constexpr auto kPanelAutoHideDelay = std::chrono::seconds(3);
constexpr float kThumbstickPressThreshold = 0.65f;
constexpr double kThumbstickSeekSpeedMsPerSecond = 20000.0;

bool EnsureLoaderInitialized(JavaVM* jvm, jobject appContext) {
    if (pfnXrInitializeLoaderKHR) return true;
    XrResult r = xrGetInstanceProcAddr(
        XR_NULL_HANDLE, "xrInitializeLoaderKHR",
        reinterpret_cast<PFN_xrVoidFunction*>(&pfnXrInitializeLoaderKHR));
    if (XR_FAILED(r) || !pfnXrInitializeLoaderKHR) {
        LOGE("xrInitializeLoaderKHR proc not found");
        return false;
    }
    XrLoaderInitInfoAndroidKHR init = { XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR };
    init.applicationVM = jvm;
    init.applicationContext = appContext;
    XrResult r2 = pfnXrInitializeLoaderKHR(reinterpret_cast<XrLoaderInitInfoBaseHeaderKHR*>(&init));
    if (XR_FAILED(r2)) {
        LOGE("xrInitializeLoaderKHR failed: %d", r2);
        return false;
    }
    return true;
}

JNIEnv* AttachThread(JavaVM* jvm) {
    JNIEnv* env = nullptr;
    if (jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) return env;
    JavaVMAttachArgs args{ JNI_VERSION_1_6, "vrp-render", nullptr };
    if (jvm->AttachCurrentThread(&env, &args) != JNI_OK) return nullptr;
    return env;
}

bool IsInstanceExtensionAvailable(const char* name) {
    uint32_t count = 0;
    XrResult countResult = xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr);
    if (XR_FAILED(countResult) || count == 0) return false;

    std::vector<XrExtensionProperties> extensions(count, { XR_TYPE_EXTENSION_PROPERTIES });
    XrResult enumResult = xrEnumerateInstanceExtensionProperties(nullptr, count, &count, extensions.data());
    if (XR_FAILED(enumResult)) return false;

    for (const auto& extension : extensions) {
        if (std::strcmp(extension.extensionName, name) == 0) return true;
    }
    return false;
}

bool Normalize3(float& x, float& y, float& z) {
    float len = std::sqrt(x * x + y * y + z * z);
    if (len <= 1e-5f) return false;
    x /= len;
    y /= len;
    z /= len;
    return true;
}

int AxisDirection(float value) {
    if (value >= kThumbstickPressThreshold) return 1;
    if (value <= -kThumbstickPressThreshold) return -1;
    return 0;
}

void RotateVectorByQuat(const XrQuaternionf& q,
                        float vx, float vy, float vz,
                        float& ox, float& oy, float& oz) {
    float uvx = q.y * vz - q.z * vy;
    float uvy = q.z * vx - q.x * vz;
    float uvz = q.x * vy - q.y * vx;

    float uuvx = q.y * uvz - q.z * uvy;
    float uuvy = q.z * uvx - q.x * uvz;
    float uuvz = q.x * uvy - q.y * uvx;

    ox = vx + 2.0f * (q.w * uvx + uuvx);
    oy = vy + 2.0f * (q.w * uvy + uuvy);
    oz = vz + 2.0f * (q.w * uvz + uuvz);
}

}  // namespace

OpenXRApp& OpenXRApp::Get() {
    static OpenXRApp inst;
    return inst;
}

void OpenXRApp::Create(JNIEnv* env, JavaVM* jvm, jobject activity) {
    if (running_.load()) return;
    jvm_ = jvm;
    sessionActive_ = false;
    textureReady_ = false;
    initFailed_ = false;

    jclass activityCls = env->GetObjectClass(activity);
    jmethodID getApplicationContext =
        env->GetMethodID(activityCls, "getApplicationContext", "()Landroid/content/Context;");
    jobject appContext = getApplicationContext ? env->CallObjectMethod(activity, getApplicationContext) : nullptr;
    env->DeleteLocalRef(activityCls);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        appContext = nullptr;
    }

    jobject loaderContext = appContext ? appContext : activity;
    appContextGlobal_ = env->NewGlobalRef(loaderContext);
    activityGlobal_ = env->NewGlobalRef(activity);
    if (!appContextGlobal_ || !activityGlobal_) {
        LOGE("Create: failed to create Android global refs");
        if (appContextGlobal_) { env->DeleteGlobalRef(appContextGlobal_); appContextGlobal_ = nullptr; }
        if (activityGlobal_) { env->DeleteGlobalRef(activityGlobal_); activityGlobal_ = nullptr; }
        if (appContext) env->DeleteLocalRef(appContext);
        initFailed_ = true;
        return;
    }
    if (!EnsureLoaderInitialized(jvm_, loaderContext)) {
        env->DeleteGlobalRef(appContextGlobal_);
        env->DeleteGlobalRef(activityGlobal_);
        appContextGlobal_ = nullptr;
        activityGlobal_ = nullptr;
        if (appContext) env->DeleteLocalRef(appContext);
        initFailed_ = true;
        return;
    }
    if (!CreateXrInstance(env, activity)) {
        env->DeleteGlobalRef(appContextGlobal_);
        env->DeleteGlobalRef(activityGlobal_);
        appContextGlobal_ = nullptr;
        activityGlobal_ = nullptr;
        if (appContext) env->DeleteLocalRef(appContext);
        initFailed_ = true;
        return;
    }
    if (appContext) env->DeleteLocalRef(appContext);

    running_ = true;
    renderThread_ = std::thread(&OpenXRApp::RenderThreadMain, this);
}

void OpenXRApp::Destroy() {
    running_ = false;
    resumed_ = false;
    if (renderThread_.joinable()) renderThread_.join();
    JNIEnv* env = AttachThread(jvm_);
    if (env) {
        std::lock_guard<std::mutex> g(pendingMutex_);
        if (pendingSurfaceTexture_) {
            env->DeleteGlobalRef(pendingSurfaceTexture_);
            pendingSurfaceTexture_ = nullptr;
        }
        needsAttach_ = false;
        if (callbackGlobal_) { env->DeleteGlobalRef(callbackGlobal_); callbackGlobal_ = nullptr; }
        if (activityGlobal_) { env->DeleteGlobalRef(activityGlobal_); activityGlobal_ = nullptr; }
        if (appContextGlobal_) { env->DeleteGlobalRef(appContextGlobal_); appContextGlobal_ = nullptr; }
    }
    midPlayPause_ = midSeekRel_ = midSeekFrac_ = midExit_ = nullptr;
    textureReady_ = false;
    initFailed_ = false;
}

void OpenXRApp::Resume() { resumed_ = true; }
void OpenXRApp::Pause()  { resumed_ = false; }

void OpenXRApp::SetCallback(JNIEnv* env, jobject callback) {
    if (callbackGlobal_) {
        env->DeleteGlobalRef(callbackGlobal_);
        callbackGlobal_ = nullptr;
        midPlayPause_ = midSeekRel_ = midSeekFrac_ = nullptr;
    }
    if (callback) {
        callbackGlobal_ = env->NewGlobalRef(callback);
        jclass cls = env->GetObjectClass(callbackGlobal_);
        midPlayPause_ = env->GetMethodID(cls, "onPlayPause", "()V");
        midSeekRel_   = env->GetMethodID(cls, "onSeekRelative", "(J)V");
        midSeekFrac_  = env->GetMethodID(cls, "onSeekFraction", "(F)V");
        midExit_      = env->GetMethodID(cls, "onExit", "()V");
        env->DeleteLocalRef(cls);
    }
}

GLuint OpenXRApp::AcquireVideoTextureId() {
    while (running_.load() &&
           !initFailed_.load(std::memory_order_acquire) &&
           !textureReady_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!textureReady_.load(std::memory_order_acquire)) {
        LOGE("AcquireVideoTextureId: renderer did not create a video texture");
        return 0;
    }
    return videoTex_.TextureId();
}

void OpenXRApp::BindVideoSurfaceTexture(JNIEnv* env, jobject surfaceTexture) {
    std::lock_guard<std::mutex> g(pendingMutex_);
    if (pendingSurfaceTexture_) {
        env->DeleteGlobalRef(pendingSurfaceTexture_);
        pendingSurfaceTexture_ = nullptr;
    }
    pendingSurfaceTexture_ = surfaceTexture ? env->NewGlobalRef(surfaceTexture) : nullptr;
    needsAttach_ = (pendingSurfaceTexture_ != nullptr);
}

// =============================================================================
// Render thread

void OpenXRApp::RenderThreadMain() {
    LOGI("render thread starting");
    JNIEnv* env = AttachThread(jvm_);
    if (!env) {
        LOGE("render thread JNIEnv attach failed");
        initFailed_ = true;
        running_ = false;
        return;
    }

    if (!InitXr(env)) {
        LOGE("InitXr failed; render thread exiting");
        initFailed_ = true;
        running_ = false;
        DestroyXr();
        if (jvm_) jvm_->DetachCurrentThread();
        return;
    }
    if (!InitEgl()) {
        LOGE("InitEgl failed");
        initFailed_ = true;
        running_ = false;
        DestroyXr();
        if (jvm_) jvm_->DetachCurrentThread();
        return;
    }
    if (!InitGlResources()) {
        LOGE("InitGlResources failed");
        initFailed_ = true;
        running_ = false;
        DestroyGlResources();
        DestroyEgl();
        DestroyXr();
        if (jvm_) jvm_->DetachCurrentThread();
        return;
    }

    while (running_.load()) {
        HandleEvents();

        // Apply pending SurfaceTexture binding on the GL thread.
        {
            std::lock_guard<std::mutex> g(pendingMutex_);
            if (needsAttach_) {
                videoTex_.SetSurfaceTexture(env, pendingSurfaceTexture_);
                if (pendingSurfaceTexture_) {
                    env->DeleteGlobalRef(pendingSurfaceTexture_);
                    pendingSurfaceTexture_ = nullptr;
                }
                videoTex_.AttachToGl(env);
                needsAttach_ = false;
            }
        }

        if (sessionActive_.load()) {
            RenderFrame(env);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    DestroyGlResources();
    if (session_ != XR_NULL_HANDLE) {
        XR_TRY(xrEndSession(session_));
    }
    DestroyEgl();
    DestroyXr();
    if (jvm_) jvm_->DetachCurrentThread();
    LOGI("render thread exited");
}

// =============================================================================
// XR

bool OpenXRApp::CreateXrInstance(JNIEnv* env, jobject activity) {
    std::vector<const char*> extensions = {
        XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
        XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
    };
    if (IsInstanceExtensionAvailable(XR_META_TOUCH_CONTROLLER_PLUS_EXTENSION_NAME)) {
        extensions.push_back(XR_META_TOUCH_CONTROLLER_PLUS_EXTENSION_NAME);
        LOGI("enabled optional extension %s", XR_META_TOUCH_CONTROLLER_PLUS_EXTENSION_NAME);
    }

    XrInstanceCreateInfoAndroidKHR androidInfo{ XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR };
    androidInfo.applicationVM = jvm_;
    androidInfo.applicationActivity = activity;

    XrApplicationInfo appInfo{};
    std::strncpy(appInfo.applicationName, "vr-player", XR_MAX_APPLICATION_NAME_SIZE - 1);
    appInfo.applicationVersion = 1;
    std::strncpy(appInfo.engineName, "custom", XR_MAX_ENGINE_NAME_SIZE - 1);
    appInfo.engineVersion = 1;
    appInfo.apiVersion = XR_CURRENT_API_VERSION;

    XrInstanceCreateInfo ci{ XR_TYPE_INSTANCE_CREATE_INFO };
    ci.next = &androidInfo;
    ci.applicationInfo = appInfo;
    ci.enabledExtensionCount = uint32_t(extensions.size());
    ci.enabledExtensionNames = extensions.data();

    XR_OK(xrCreateInstance(&ci, &instance_));
    LOGI("xrCreateInstance ok instance=%p", instance_);
    return true;
}

bool OpenXRApp::InitXr(JNIEnv* env) {
    if (instance_ == XR_NULL_HANDLE) {
        LOGE("InitXr: OpenXR instance was not created");
        return false;
    }

    XrSystemGetInfo sysInfo{ XR_TYPE_SYSTEM_GET_INFO };
    sysInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XR_OK(xrGetSystem(instance_, &sysInfo, &systemId_));
    LOGI("system id=%llu", (unsigned long long)systemId_);

    uint32_t viewCount = 0;
    XR_OK(xrEnumerateViewConfigurationViews(
        instance_, systemId_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr));
    if (viewCount != 2) { LOGE("expected 2 views, got %u", viewCount); return false; }
    for (auto& v : viewConfigs_) v = { XR_TYPE_VIEW_CONFIGURATION_VIEW };
    XR_OK(xrEnumerateViewConfigurationViews(
        instance_, systemId_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &viewCount, viewConfigs_));
    LOGI("eye recommended=%dx%d", viewConfigs_[0].recommendedImageRectWidth,
         viewConfigs_[0].recommendedImageRectHeight);

    return true;
}

void OpenXRApp::DestroyXr() {
    if (aimSpace_[0]) { xrDestroySpace(aimSpace_[0]); aimSpace_[0] = XR_NULL_HANDLE; }
    if (aimSpace_[1]) { xrDestroySpace(aimSpace_[1]); aimSpace_[1] = XR_NULL_HANDLE; }
    if (actionSet_)   { xrDestroyActionSet(actionSet_); actionSet_ = XR_NULL_HANDLE; }
    if (swapchain_)   { xrDestroySwapchain(swapchain_); swapchain_ = XR_NULL_HANDLE; }
    if (localSpace_)  { xrDestroySpace(localSpace_); localSpace_ = XR_NULL_HANDLE; }
    if (session_)     { xrDestroySession(session_); session_ = XR_NULL_HANDLE; }
    if (instance_)    { xrDestroyInstance(instance_); instance_ = XR_NULL_HANDLE; }
}

bool OpenXRApp::InitEgl() {
    eglDisplay_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (eglDisplay_ == EGL_NO_DISPLAY) { LOGE("eglGetDisplay failed"); return false; }
    EGLint major = 0, minor = 0;
    if (!eglInitialize(eglDisplay_, &major, &minor)) { LOGE("eglInitialize failed"); return false; }
    LOGI("EGL initialised %d.%d", major, minor);

    const EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0,
        EGL_STENCIL_SIZE, 0,
        EGL_SAMPLES, 0,
        EGL_NONE,
    };
    EGLint numConfigs = 0;
    if (!eglChooseConfig(eglDisplay_, configAttribs, &eglConfig_, 1, &numConfigs) || numConfigs == 0) {
        LOGE("eglChooseConfig failed");
        return false;
    }

    const EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    eglContext_ = eglCreateContext(eglDisplay_, eglConfig_, EGL_NO_CONTEXT, contextAttribs);
    if (eglContext_ == EGL_NO_CONTEXT) { LOGE("eglCreateContext failed"); return false; }

    const EGLint pbufAttribs[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
    eglPbuffer_ = eglCreatePbufferSurface(eglDisplay_, eglConfig_, pbufAttribs);
    if (eglPbuffer_ == EGL_NO_SURFACE) { LOGE("eglCreatePbufferSurface failed"); return false; }

    if (!eglMakeCurrent(eglDisplay_, eglPbuffer_, eglPbuffer_, eglContext_)) {
        LOGE("eglMakeCurrent failed");
        return false;
    }
    LOGI("EGL context current");
    return true;
}

void OpenXRApp::DestroyEgl() {
    if (eglDisplay_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (eglPbuffer_ != EGL_NO_SURFACE) eglDestroySurface(eglDisplay_, eglPbuffer_);
        if (eglContext_ != EGL_NO_CONTEXT) eglDestroyContext(eglDisplay_, eglContext_);
        eglTerminate(eglDisplay_);
    }
    eglDisplay_ = EGL_NO_DISPLAY;
    eglContext_ = EGL_NO_CONTEXT;
    eglPbuffer_ = EGL_NO_SURFACE;
    eglConfig_  = nullptr;
}

bool OpenXRApp::InitGlResources() {
    // OpenGL ES requirements check (must be called after instance, before session).
    PFN_xrVoidFunction reqFn = nullptr;
    XR_OK(xrGetInstanceProcAddr(instance_, "xrGetOpenGLESGraphicsRequirementsKHR", &reqFn));
    auto pfnReq = reinterpret_cast<PFN_xrGetOpenGLESGraphicsRequirementsKHR>(reqFn);
    XrGraphicsRequirementsOpenGLESKHR req{ XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR };
    XR_OK(pfnReq(instance_, systemId_, &req));
    LOGI("OpenGL ES required min=0x%llx max=0x%llx",
         (unsigned long long)req.minApiVersionSupported,
         (unsigned long long)req.maxApiVersionSupported);

    // Session.
    XrGraphicsBindingOpenGLESAndroidKHR binding{ XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR };
    binding.display = eglDisplay_;
    binding.config  = eglConfig_;
    binding.context = eglContext_;

    XrSessionCreateInfo sci{ XR_TYPE_SESSION_CREATE_INFO };
    sci.next = &binding;
    sci.systemId = systemId_;
    XR_OK(xrCreateSession(instance_, &sci, &session_));
    LOGI("xrCreateSession ok");

    XrReferenceSpaceCreateInfo rsci{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    rsci.poseInReferenceSpace.orientation = { 0, 0, 0, 1 };
    rsci.poseInReferenceSpace.position = { 0, 0, 0 };
    XR_OK(xrCreateReferenceSpace(session_, &rsci, &localSpace_));

    // Swapchain — single multiview swapchain with 2 array layers.
    swapchainWidth_  = int(viewConfigs_[0].recommendedImageRectWidth);
    swapchainHeight_ = int(viewConfigs_[0].recommendedImageRectHeight);

    XrSwapchainCreateInfo scci{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
    scci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    scci.format = GL_SRGB8_ALPHA8;
    scci.sampleCount = 1;
    scci.width  = uint32_t(swapchainWidth_);
    scci.height = uint32_t(swapchainHeight_);
    scci.faceCount = 1;
    scci.arraySize = 2;
    scci.mipCount = 1;
    XR_OK(xrCreateSwapchain(session_, &scci, &swapchain_));

    uint32_t imgCount = 0;
    XR_OK(xrEnumerateSwapchainImages(swapchain_, 0, &imgCount, nullptr));
    swapchainImages_.assign(imgCount, { XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR });
    XR_OK(xrEnumerateSwapchainImages(
        swapchain_, imgCount, &imgCount,
        reinterpret_cast<XrSwapchainImageBaseHeader*>(swapchainImages_.data())));
    LOGI("swapchain %dx%d images=%u", swapchainWidth_, swapchainHeight_, imgCount);

    // Actions: trigger (float) + aim pose, on left/right hand.
    XrActionSetCreateInfo asci{ XR_TYPE_ACTION_SET_CREATE_INFO };
    std::strcpy(asci.actionSetName, "vrp_panel");
    std::strcpy(asci.localizedActionSetName, "VR Player Panel");
    asci.priority = 0;
    XR_OK(xrCreateActionSet(instance_, &asci, &actionSet_));

    XR_OK(xrStringToPath(instance_, "/user/hand/left",  &subactionPaths_[0]));
    XR_OK(xrStringToPath(instance_, "/user/hand/right", &subactionPaths_[1]));

    {
        XrActionCreateInfo aci{ XR_TYPE_ACTION_CREATE_INFO };
        std::strcpy(aci.actionName, "trigger");
        std::strcpy(aci.localizedActionName, "Select");
        aci.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
        aci.countSubactionPaths = 2;
        aci.subactionPaths = subactionPaths_;
        XR_OK(xrCreateAction(actionSet_, &aci, &triggerAction_));
    }
    {
        XrActionCreateInfo aci{ XR_TYPE_ACTION_CREATE_INFO };
        std::strcpy(aci.actionName, "aim");
        std::strcpy(aci.localizedActionName, "Aim Pose");
        aci.actionType = XR_ACTION_TYPE_POSE_INPUT;
        aci.countSubactionPaths = 2;
        aci.subactionPaths = subactionPaths_;
        XR_OK(xrCreateAction(actionSet_, &aci, &aimPoseAction_));
    }
    {
        XrActionCreateInfo aci{ XR_TYPE_ACTION_CREATE_INFO };
        std::strcpy(aci.actionName, "button_a");
        std::strcpy(aci.localizedActionName, "A");
        aci.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
        aci.countSubactionPaths = 1;
        aci.subactionPaths = &subactionPaths_[1];
        XR_OK(xrCreateAction(actionSet_, &aci, &buttonAAction_));
    }
    {
        XrActionCreateInfo aci{ XR_TYPE_ACTION_CREATE_INFO };
        std::strcpy(aci.actionName, "button_b");
        std::strcpy(aci.localizedActionName, "B");
        aci.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
        aci.countSubactionPaths = 1;
        aci.subactionPaths = &subactionPaths_[1];
        XR_OK(xrCreateAction(actionSet_, &aci, &buttonBAction_));
    }
    {
        XrActionCreateInfo aci{ XR_TYPE_ACTION_CREATE_INFO };
        std::strcpy(aci.actionName, "thumbstick_x");
        std::strcpy(aci.localizedActionName, "Thumbstick X");
        aci.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
        aci.countSubactionPaths = 2;
        aci.subactionPaths = subactionPaths_;
        XR_OK(xrCreateAction(actionSet_, &aci, &thumbstickXAction_));
    }
    {
        XrActionCreateInfo aci{ XR_TYPE_ACTION_CREATE_INFO };
        std::strcpy(aci.actionName, "thumbstick_y");
        std::strcpy(aci.localizedActionName, "Thumbstick Y");
        aci.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
        aci.countSubactionPaths = 2;
        aci.subactionPaths = subactionPaths_;
        XR_OK(xrCreateAction(actionSet_, &aci, &thumbstickYAction_));
    }
    {
        XrActionCreateInfo aci{ XR_TYPE_ACTION_CREATE_INFO };
        std::strcpy(aci.actionName, "thumbstick_click");
        std::strcpy(aci.localizedActionName, "Thumbstick Click");
        aci.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
        aci.countSubactionPaths = 2;
        aci.subactionPaths = subactionPaths_;
        XR_OK(xrCreateAction(actionSet_, &aci, &thumbstickClickAction_));
    }

    XrPath leftTrig, rightTrig, leftAim, rightAim;
    XrPath rightA, rightB;
    XrPath leftThumbX, rightThumbX, leftThumbY, rightThumbY;
    XrPath leftThumbClick, rightThumbClick;
    XR_OK(xrStringToPath(instance_, "/user/hand/left/input/trigger/value", &leftTrig));
    XR_OK(xrStringToPath(instance_, "/user/hand/right/input/trigger/value", &rightTrig));
    XR_OK(xrStringToPath(instance_, "/user/hand/left/input/aim/pose", &leftAim));
    XR_OK(xrStringToPath(instance_, "/user/hand/right/input/aim/pose", &rightAim));
    XR_OK(xrStringToPath(instance_, "/user/hand/right/input/a/click", &rightA));
    XR_OK(xrStringToPath(instance_, "/user/hand/right/input/b/click", &rightB));
    XR_OK(xrStringToPath(instance_, "/user/hand/left/input/thumbstick/x", &leftThumbX));
    XR_OK(xrStringToPath(instance_, "/user/hand/right/input/thumbstick/x", &rightThumbX));
    XR_OK(xrStringToPath(instance_, "/user/hand/left/input/thumbstick/y", &leftThumbY));
    XR_OK(xrStringToPath(instance_, "/user/hand/right/input/thumbstick/y", &rightThumbY));
    XR_OK(xrStringToPath(instance_, "/user/hand/left/input/thumbstick/click", &leftThumbClick));
    XR_OK(xrStringToPath(instance_, "/user/hand/right/input/thumbstick/click", &rightThumbClick));

    std::array<XrActionSuggestedBinding, 12> suggested = {{
        { triggerAction_, leftTrig },
        { triggerAction_, rightTrig },
        { aimPoseAction_, leftAim },
        { aimPoseAction_, rightAim },
        { buttonAAction_, rightA },
        { buttonBAction_, rightB },
        { thumbstickXAction_, leftThumbX },
        { thumbstickXAction_, rightThumbX },
        { thumbstickYAction_, leftThumbY },
        { thumbstickYAction_, rightThumbY },
        { thumbstickClickAction_, leftThumbClick },
        { thumbstickClickAction_, rightThumbClick },
    }};
    auto suggestBindings = [&](const char* profilePath) -> bool {
        XrPath profile = XR_NULL_PATH;
        XrResult pathResult = xrStringToPath(instance_, profilePath, &profile);
        if (XR_FAILED(pathResult)) {
            LOGW("controller profile path unsupported: %s result=%d", profilePath, pathResult);
            return false;
        }
        XrInteractionProfileSuggestedBinding ipsb{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
        ipsb.interactionProfile = profile;
        ipsb.countSuggestedBindings = uint32_t(suggested.size());
        ipsb.suggestedBindings = suggested.data();
        XrResult suggestResult = xrSuggestInteractionProfileBindings(instance_, &ipsb);
        if (XR_FAILED(suggestResult)) {
            LOGW("controller binding suggestion failed: %s result=%d", profilePath, suggestResult);
            return false;
        }
        LOGI("controller bindings suggested for %s", profilePath);
        return true;
    };

    bool suggestedAny = false;
    suggestedAny |= suggestBindings("/interaction_profiles/meta/touch_plus_controller");
    suggestedAny |= suggestBindings("/interaction_profiles/oculus/touch_controller");
    if (!suggestedAny) {
        LOGE("failed to suggest bindings for any Touch controller profile");
        return false;
    }

    {
        XrSessionActionSetsAttachInfo ai{ XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
        ai.countActionSets = 1;
        ai.actionSets = &actionSet_;
        XR_OK(xrAttachSessionActionSets(session_, &ai));
    }

    for (int i = 0; i < 2; ++i) {
        XrActionSpaceCreateInfo asci{ XR_TYPE_ACTION_SPACE_CREATE_INFO };
        asci.action = aimPoseAction_;
        asci.subactionPath = subactionPaths_[i];
        asci.poseInActionSpace.orientation = { 0, 0, 0, 1 };
        XR_OK(xrCreateActionSpace(session_, &asci, &aimSpace_[i]));
    }

    // GL resources.
    videoProgram_ = gl::LinkProgram(shaders::kVideoVert, shaders::kVideoFrag);
    panelProgram_ = gl::LinkProgram(shaders::kPanelVert, shaders::kPanelFrag);
    pointerProgram_ = gl::LinkProgram(shaders::kPointerVert, shaders::kPointerFrag);
    if (!videoProgram_ || !panelProgram_ || !pointerProgram_) return false;

    mesh_.Create(2.0f, 180.0f, 180.0f);
    panel_.Create();
    if (!CreatePointerResources()) return false;
    panelVisible_ = true;
    panelLastInteractionTime_ = std::chrono::steady_clock::now();
    lastThumbstickSeekTime_ = panelLastInteractionTime_;
    videoTex_.CreateTexture();
    fbo_.Create(swapchainWidth_, swapchainHeight_);
    textureReady_ = true;

    return true;
}

void OpenXRApp::DestroyGlResources() {
    JNIEnv* env = AttachThread(jvm_);
    if (env) videoTex_.ClearSurfaceTexture(env);
    fbo_.Destroy();
    videoTex_.Destroy();
    DestroyPointerResources();
    panel_.Destroy();
    mesh_.Destroy();
    if (pointerProgram_) { glDeleteProgram(pointerProgram_); pointerProgram_ = 0; }
    if (panelProgram_) { glDeleteProgram(panelProgram_); panelProgram_ = 0; }
    if (videoProgram_) { glDeleteProgram(videoProgram_); videoProgram_ = 0; }
}

bool OpenXRApp::CreatePointerResources() {
    glGenVertexArrays(1, &pointerVao_);
    glBindVertexArray(pointerVao_);

    glGenBuffers(1, &pointerVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, pointerVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6, nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, nullptr);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return pointerVao_ != 0 && pointerVbo_ != 0;
}

void OpenXRApp::DestroyPointerResources() {
    if (pointerVbo_) { glDeleteBuffers(1, &pointerVbo_); pointerVbo_ = 0; }
    if (pointerVao_) { glDeleteVertexArrays(1, &pointerVao_); pointerVao_ = 0; }
}

bool OpenXRApp::LocateControllerRay(int hand, XrTime displayTime, ControllerRay& ray) {
    ray = {};
    if (hand < 0 || hand >= 2 || aimSpace_[hand] == XR_NULL_HANDLE) return false;

    XrSpaceLocation location{ XR_TYPE_SPACE_LOCATION };
    XrResult result = xrLocateSpace(aimSpace_[hand], localSpace_, displayTime, &location);
    if (XR_FAILED(result)) return false;

    constexpr XrSpaceLocationFlags kRequiredFlags =
        XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if ((location.locationFlags & kRequiredFlags) != kRequiredFlags) return false;

    const XrPosef& pose = location.pose;
    ray.origin[0] = pose.position.x;
    ray.origin[1] = pose.position.y;
    ray.origin[2] = pose.position.z;

    RotateVectorByQuat(pose.orientation, 0.0f, 0.0f, -1.0f,
                       ray.direction[0], ray.direction[1], ray.direction[2]);
    if (!Normalize3(ray.direction[0], ray.direction[1], ray.direction[2])) return false;

    ray.hover = panel_.HitTestRay(ray.origin[0], ray.origin[1], ray.origin[2],
                                  ray.direction[0], ray.direction[1], ray.direction[2]);
    ray.valid = true;
    return true;
}

void OpenXRApp::DrawControllerRay(const ControllerRay& ray,
                                  const gl::Mat4& view0, const gl::Mat4& view1,
                                  const gl::Mat4& proj0, const gl::Mat4& proj1,
                                  const float color[4]) {
    if (!ray.valid || !pointerProgram_ || !pointerVao_ || !pointerVbo_) return;

    constexpr float kDefaultRayMeters = 2.5f;
    bool hitPanel = ray.hover.u >= 0.0f && ray.hover.v >= 0.0f && ray.hover.rayDistance > 0.0f;
    float length = hitPanel ? ray.hover.rayDistance : kDefaultRayMeters;
    float verts[6] = {
        ray.origin[0],
        ray.origin[1],
        ray.origin[2],
        ray.origin[0] + ray.direction[0] * length,
        ray.origin[1] + ray.direction[1] * length,
        ray.origin[2] + ray.direction[2] * length,
    };

    glUseProgram(pointerProgram_);

    GLint locView = glGetUniformLocation(pointerProgram_, "u_view");
    GLint locProj = glGetUniformLocation(pointerProgram_, "u_proj");
    GLint locColor = glGetUniformLocation(pointerProgram_, "u_color");

    float views[32];
    std::memcpy(views,      view0.m, sizeof(view0.m));
    std::memcpy(views + 16, view1.m, sizeof(view1.m));
    glUniformMatrix4fv(locView, 2, GL_FALSE, views);

    float projs[32];
    std::memcpy(projs,      proj0.m, sizeof(proj0.m));
    std::memcpy(projs + 16, proj1.m, sizeof(proj1.m));
    glUniformMatrix4fv(locProj, 2, GL_FALSE, projs);
    glUniform4fv(locColor, 1, color);

    glBindVertexArray(pointerVao_);
    glBindBuffer(GL_ARRAY_BUFFER, pointerVbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glLineWidth(3.0f);
    glDrawArrays(GL_LINES, 0, 2);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void OpenXRApp::HandleEvents() {
    while (true) {
        XrEventDataBuffer ev{ XR_TYPE_EVENT_DATA_BUFFER };
        XrResult r = xrPollEvent(instance_, &ev);
        if (r == XR_EVENT_UNAVAILABLE) break;
        if (XR_FAILED(r)) break;

        switch (ev.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                auto* e = reinterpret_cast<XrEventDataSessionStateChanged*>(&ev);
                sessionState_ = e->state;
                LOGI("session state -> %d", e->state);
                if (e->state == XR_SESSION_STATE_READY) {
                    XrSessionBeginInfo bi{ XR_TYPE_SESSION_BEGIN_INFO };
                    bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    XR_TRY(xrBeginSession(session_, &bi));
                    sessionActive_ = true;
                } else if (e->state == XR_SESSION_STATE_STOPPING) {
                    XR_TRY(xrEndSession(session_));
                    sessionActive_ = false;
                } else if (e->state == XR_SESSION_STATE_EXITING || e->state == XR_SESSION_STATE_LOSS_PENDING) {
                    sessionActive_ = false;
                    running_ = false;
                }
                break;
            }
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
                LOGW("instance loss pending");
                running_ = false;
                break;
            }
            default: break;
        }
    }
}

void OpenXRApp::RenderFrame(JNIEnv* env) {
    XrFrameWaitInfo waitInfo{ XR_TYPE_FRAME_WAIT_INFO };
    XrFrameState frameState{ XR_TYPE_FRAME_STATE };
    XR_TRY(xrWaitFrame(session_, &waitInfo, &frameState));

    XrFrameBeginInfo beginInfo{ XR_TYPE_FRAME_BEGIN_INFO };
    XR_TRY(xrBeginFrame(session_, &beginInfo));

    XrCompositionLayerProjectionView projViews[2]{};
    XrCompositionLayerProjection projLayer{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    std::vector<XrCompositionLayerBaseHeader*> layers;

    if (frameState.shouldRender) {
        // Sync actions.
        XrActiveActionSet active{ actionSet_, XR_NULL_PATH };
        XrActionsSyncInfo syncInfo{ XR_TYPE_ACTIONS_SYNC_INFO };
        syncInfo.countActiveActionSets = 1;
        syncInfo.activeActionSets = &active;
        XR_TRY(xrSyncActions(session_, &syncInfo));

        // Locate views.
        XrViewLocateInfo vli{ XR_TYPE_VIEW_LOCATE_INFO };
        vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        vli.displayTime = frameState.predictedDisplayTime;
        vli.space = localSpace_;
        XrViewState vs{ XR_TYPE_VIEW_STATE };
        XrView views[2] = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
        uint32_t viewCount = 0;
        XR_TRY(xrLocateViews(session_, &vli, &vs, 2, &viewCount, views));
        if (viewCount != 2) {
            XrFrameEndInfo endInfo{ XR_TYPE_FRAME_END_INFO };
            endInfo.displayTime = frameState.predictedDisplayTime;
            endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
            XR_TRY(xrEndFrame(session_, &endInfo));
            return;
        }

        // Build per-eye matrices.
        gl::Mat4 viewMat[2]{}, projMat[2]{};
        for (int i = 0; i < 2; ++i) {
            const auto& p = views[i].pose;
            const auto& f = views[i].fov;
            viewMat[i] = gl::ViewFromPose(p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w,
                                          p.position.x, p.position.y, p.position.z);
            projMat[i] = gl::ProjectionFromXrFov(f.angleLeft, f.angleRight, f.angleUp, f.angleDown, 0.1f, 100.0f);
        }

        // Pull next video frame, if any.
        videoTex_.UpdateIfNeeded(env);

        // Update panel state.
        long long dur = durationMs_.load();
        long long cur = currentMs_.load();
        float progress = (dur > 0) ? float(double(cur) / double(dur)) : 0.0f;
        panel_.SetIsPlaying(isPlaying_.load());
        panel_.SetIsLoading(isLoading_.load());
        panel_.SetProgress(progress);
        panel_.SetTimes(cur, dur);

        // Quest Touch controllers are the only control source. Head gaze is
        // intentionally not used, so no fixed eye-centered cursor can activate UI.
        ControllerRay controllerRays[2];
        for (int hand = 0; hand < 2; ++hand) {
            LocateControllerRay(hand, frameState.predictedDisplayTime, controllerRays[hand]);
        }

        hover_ = {};
        for (int hand = 1; hand >= 0; --hand) {
            if (controllerRays[hand].hover.u >= 0.0f && controllerRays[hand].hover.v >= 0.0f) {
                hover_ = controllerRays[hand].hover;
                break;
            }
        }

        const auto now = std::chrono::steady_clock::now();
        bool inputStarted = false;
        bool consumedForWake = false;

        XrActionStateGetInfo buttonGi{ XR_TYPE_ACTION_STATE_GET_INFO };
        buttonGi.subactionPath = subactionPaths_[1];

        buttonGi.action = buttonAAction_;
        XrActionStateBoolean buttonAState{ XR_TYPE_ACTION_STATE_BOOLEAN };
        XrResult buttonAResult = xrGetActionStateBoolean(session_, &buttonGi, &buttonAState);
        bool buttonAPressed = XR_SUCCEEDED(buttonAResult) && buttonAState.isActive && buttonAState.currentState;
        bool buttonAJustPressed = buttonAPressed && !prevButtonA_;
        prevButtonA_ = buttonAPressed;
        inputStarted |= buttonAJustPressed;

        buttonGi.action = buttonBAction_;
        XrActionStateBoolean buttonBState{ XR_TYPE_ACTION_STATE_BOOLEAN };
        XrResult buttonBResult = xrGetActionStateBoolean(session_, &buttonGi, &buttonBState);
        bool buttonBPressed = XR_SUCCEEDED(buttonBResult) && buttonBState.isActive && buttonBState.currentState;
        bool buttonBJustPressed = buttonBPressed && !prevButtonB_;
        prevButtonB_ = buttonBPressed;
        inputStarted |= buttonBJustPressed;

        bool triggerJustPressed[2] = { false, false };
        bool thumbstickClickJustPressed[2] = { false, false };
        float thumbstickXValue[2] = { 0.0f, 0.0f };
        bool thumbstickXHeld[2] = { false, false };
        int thumbstickYJustDir[2] = { 0, 0 };
        bool continuousInputActive = false;

        for (int hand = 0; hand < 2; ++hand) {
            XrActionStateGetInfo gi{ XR_TYPE_ACTION_STATE_GET_INFO };
            gi.subactionPath = subactionPaths_[hand];

            gi.action = triggerAction_;
            XrActionStateFloat trigState{ XR_TYPE_ACTION_STATE_FLOAT };
            XrResult triggerResult = xrGetActionStateFloat(session_, &gi, &trigState);
            bool pressed = XR_SUCCEEDED(triggerResult) && trigState.isActive && trigState.currentState > 0.5f;
            triggerJustPressed[hand] = pressed && !prevTrigger_[hand];
            prevTrigger_[hand] = pressed;
            inputStarted |= triggerJustPressed[hand];

            gi.action = thumbstickClickAction_;
            XrActionStateBoolean thumbClickState{ XR_TYPE_ACTION_STATE_BOOLEAN };
            XrResult thumbClickResult = xrGetActionStateBoolean(session_, &gi, &thumbClickState);
            bool thumbClickPressed = XR_SUCCEEDED(thumbClickResult) &&
                                     thumbClickState.isActive &&
                                     thumbClickState.currentState;
            thumbstickClickJustPressed[hand] = thumbClickPressed && !prevThumbstickClick_[hand];
            prevThumbstickClick_[hand] = thumbClickPressed;
            inputStarted |= thumbstickClickJustPressed[hand];

            gi.action = thumbstickXAction_;
            XrActionStateFloat thumbXState{ XR_TYPE_ACTION_STATE_FLOAT };
            XrResult thumbXResult = xrGetActionStateFloat(session_, &gi, &thumbXState);
            int thumbXDir = (XR_SUCCEEDED(thumbXResult) && thumbXState.isActive)
                                ? AxisDirection(thumbXState.currentState)
                                : 0;
            thumbstickXValue[hand] = thumbXDir != 0 ? thumbXState.currentState : 0.0f;
            thumbstickXHeld[hand] = thumbXDir != 0;
            bool thumbXJustStarted = thumbXDir != 0 && thumbXDir != prevThumbstickXDir_[hand];
            prevThumbstickXDir_[hand] = thumbXDir;
            inputStarted |= thumbXJustStarted;
            continuousInputActive |= thumbstickXHeld[hand];

            gi.action = thumbstickYAction_;
            XrActionStateFloat thumbYState{ XR_TYPE_ACTION_STATE_FLOAT };
            XrResult thumbYResult = xrGetActionStateFloat(session_, &gi, &thumbYState);
            int thumbYDir = (XR_SUCCEEDED(thumbYResult) && thumbYState.isActive)
                                ? AxisDirection(thumbYState.currentState)
                                : 0;
            thumbstickYJustDir[hand] =
                (thumbYDir != 0 && thumbYDir != prevThumbstickYDir_[hand]) ? thumbYDir : 0;
            prevThumbstickYDir_[hand] = thumbYDir;
            inputStarted |= thumbstickYJustDir[hand] != 0;
        }

        double seekElapsedSeconds =
            std::chrono::duration<double>(now - lastThumbstickSeekTime_).count();
        lastThumbstickSeekTime_ = now;

        if (inputStarted) {
            panelLastInteractionTime_ = now;
            if (!panelVisible_) {
                panelVisible_ = true;
                consumedForWake = true;
            }
        }
        if (panelVisible_ && continuousInputActive) {
            panelLastInteractionTime_ = now;
        }

        if (panelVisible_ && !inputStarted && now - panelLastInteractionTime_ >= kPanelAutoHideDelay) {
            panelVisible_ = false;
            hover_ = {};
        }

        if (panelVisible_ && !consumedForWake) {
            if (buttonAJustPressed) {
                DispatchPlayPause(env);
            }
            if (buttonBJustPressed) {
                DispatchExit(env);
            }

            for (int hand = 0; hand < 2; ++hand) {
                if (thumbstickClickJustPressed[hand]) {
                    DispatchPlayPause(env);
                }
                if (thumbstickXHeld[hand] && seekElapsedSeconds > 0.0) {
                    long long deltaMs = static_cast<long long>(
                        double(thumbstickXValue[hand]) *
                        kThumbstickSeekSpeedMsPerSecond *
                        seekElapsedSeconds);
                    if (deltaMs != 0) {
                        DispatchSeekRelative(env, deltaMs);
                    }
                }
                if (thumbstickYJustDir[hand] < 0) {
                    panelVisible_ = false;
                    hover_ = {};
                }

                const auto& handHover = controllerRays[hand].hover;
                if (triggerJustPressed[hand] && handHover.region != ControlPanel::Region_None) {
                    switch (handHover.region) {
                        case ControlPanel::Region_Minus15:    DispatchSeekRelative(env, -15000); break;
                        case ControlPanel::Region_Plus15:     DispatchSeekRelative(env,  15000); break;
                        case ControlPanel::Region_PlayPause:  DispatchPlayPause(env); break;
                        case ControlPanel::Region_Scrubber:   DispatchSeekFraction(env, handHover.scrubFraction); break;
                        default: break;
                    }
                }
            }
        }

        // Acquire swapchain image.
        uint32_t imageIndex = 0;
        XrSwapchainImageAcquireInfo aci{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
        XR_TRY(xrAcquireSwapchainImage(swapchain_, &aci, &imageIndex));
        XrSwapchainImageWaitInfo swi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
        swi.timeout = XR_INFINITE_DURATION;
        XR_TRY(xrWaitSwapchainImage(swapchain_, &swi));

        GLuint colorTex = swapchainImages_[imageIndex].image;
        if (!fbo_.Bind(colorTex)) {
            LOGW("FBO bind failed");
        } else {
            glClearColor(0.f, 0.f, 0.f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // Video sphere — sampled OES texture, no depth write so it's a backdrop.
            glDepthMask(GL_FALSE);
            glDisable(GL_CULL_FACE);
            glUseProgram(videoProgram_);
            GLint locView  = glGetUniformLocation(videoProgram_, "u_view");
            GLint locProj  = glGetUniformLocation(videoProgram_, "u_proj");
            GLint locModel = glGetUniformLocation(videoProgram_, "u_model");
            GLint locTex   = glGetUniformLocation(videoProgram_, "u_videoTex");
            GLint locStereo= glGetUniformLocation(videoProgram_, "u_stereoMode");
            GLint locTexTransform = glGetUniformLocation(videoProgram_, "u_texTransform");
            float matsView[32], matsProj[32];
            std::memcpy(matsView,      viewMat[0].m, sizeof(viewMat[0].m));
            std::memcpy(matsView + 16, viewMat[1].m, sizeof(viewMat[1].m));
            std::memcpy(matsProj,      projMat[0].m, sizeof(projMat[0].m));
            std::memcpy(matsProj + 16, projMat[1].m, sizeof(projMat[1].m));
            glUniformMatrix4fv(locView, 2, GL_FALSE, matsView);
            glUniformMatrix4fv(locProj, 2, GL_FALSE, matsProj);
            gl::Mat4 model = mesh_.ModelTransform();
            glUniformMatrix4fv(locModel, 1, GL_FALSE, model.m);
            glUniform1i(locStereo, stereoMode_.load());
            glUniformMatrix4fv(locTexTransform, 1, GL_FALSE, videoTex_.TransformMatrix());

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_EXTERNAL_OES, videoTex_.TextureId());
            glUniform1i(locTex, 0);

            mesh_.Draw();

            glDepthMask(GL_TRUE);

            if (panelVisible_) {
                panel_.Draw(panelProgram_, viewMat[0], viewMat[1], projMat[0], projMat[1], hover_);

                const float leftColor[4] = { 0.25f, 0.70f, 1.0f, 0.88f };
                const float rightColor[4] = { 1.0f, 0.88f, 0.30f, 0.88f };
                DrawControllerRay(controllerRays[0], viewMat[0], viewMat[1], projMat[0], projMat[1], leftColor);
                DrawControllerRay(controllerRays[1], viewMat[0], viewMat[1], projMat[0], projMat[1], rightColor);
            }
        }

        XrSwapchainImageReleaseInfo rli{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
        XR_TRY(xrReleaseSwapchainImage(swapchain_, &rli));

        for (int i = 0; i < 2; ++i) {
            projViews[i] = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
            projViews[i].pose = views[i].pose;
            projViews[i].fov = views[i].fov;
            projViews[i].subImage.swapchain = swapchain_;
            projViews[i].subImage.imageRect = {
                {0, 0}, { swapchainWidth_, swapchainHeight_ }
            };
            projViews[i].subImage.imageArrayIndex = uint32_t(i);
        }
        projLayer.space = localSpace_;
        projLayer.viewCount = 2;
        projLayer.views = projViews;
        layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&projLayer));
    }

    XrFrameEndInfo endInfo{ XR_TYPE_FRAME_END_INFO };
    endInfo.displayTime = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = uint32_t(layers.size());
    endInfo.layers = layers.empty() ? nullptr : layers.data();
    XR_TRY(xrEndFrame(session_, &endInfo));
}

void OpenXRApp::DispatchPlayPause(JNIEnv* env) {
    if (callbackGlobal_ && midPlayPause_) {
        env->CallVoidMethod(callbackGlobal_, midPlayPause_);
        if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
    }
}

void OpenXRApp::DispatchSeekRelative(JNIEnv* env, long long deltaMs) {
    if (callbackGlobal_ && midSeekRel_) {
        env->CallVoidMethod(callbackGlobal_, midSeekRel_, jlong(deltaMs));
        if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
    }
}

void OpenXRApp::DispatchSeekFraction(JNIEnv* env, float fraction) {
    if (callbackGlobal_ && midSeekFrac_) {
        env->CallVoidMethod(callbackGlobal_, midSeekFrac_, jfloat(fraction));
        if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
    }
}

void OpenXRApp::DispatchExit(JNIEnv* env) {
    if (callbackGlobal_ && midExit_) {
        env->CallVoidMethod(callbackGlobal_, midExit_);
        if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
    }
}

}  // namespace vrp
