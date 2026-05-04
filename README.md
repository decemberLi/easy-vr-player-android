# VR Player（Quest 3）

把 visionOS 上的 [PanPlayer](https://github.com/anthropics/no-such-link) 移植到 Meta Quest 3 的原生 VR 播放器。支持本地视频（SAF 选择器）、115 网盘（OAuth 登录 + 多清晰度 + 原画走本地 HTTP 代理边下边播）、VR180 半球 SBS 立体投影，沉浸空间内含播放/暂停 / ±15s / 进度条控制面板。

## 平台与依赖

- 设备：Meta Quest 3 / Quest 3S / Quest Pro（Horizon OS）
- Android `minSdk = 29`，`targetSdk = 34`，仅 `arm64-v8a`
- Android Studio Hedgehog (2024.2) 或更新；NDK r26.3+，CMake 3.22+
- OpenXR Loader：[`org.khronos.openxr:openxr_loader_for_android`](https://repo1.maven.org/maven2/org/khronos/openxr/openxr_loader_for_android/) 1.1.36（自动通过 Gradle 拉取）
- 视频解码：AndroidX Media3 (ExoPlayer) 1.4

## 工程结构

```
vr-player/
├── settings.gradle.kts                    Gradle 配置入口
├── build.gradle.kts                       根 build
├── gradle/libs.versions.toml              依赖版本目录
└── app/
    ├── build.gradle.kts                   AGP / NDK / Compose / Media3 配置
    ├── proguard-rules.pro                 release proguard 规则
    └── src/main/
        ├── AndroidManifest.xml            VR-only 包，OpenXR queries，2D + 沉浸式 Activity
        ├── res/...                        主题、SAF 网络配置、应用图标
        ├── java/space/vrplayer/
        │   ├── VrPlayerApp.kt             Application：初始化 token + 启动本地代理
        │   ├── MainActivity.kt            2D 面板，Compose 三屏导航
        │   ├── ui/                        HomeScreen / FileList115Screen / SettingsScreen
        │   ├── auth/AuthWebViewActivity.kt 内嵌 WebView 拦截 vrplayer.space?code=...
        │   ├── cloud115/                  TokenManager115 / DataManager115 + 数据模型
        │   ├── proxy/LocalHttpProxy.kt    本地 HTTP 代理（Range / 8 MiB 钳制）
        │   ├── local/SafVideoPicker.kt    SAF 视频选择器
        │   ├── net/HttpClient.kt          全局 OkHttp
        │   ├── player/PlayerSession.kt    ExoPlayer 封装
        │   └── immersive/
        │       ├── NativeBridge.kt        JNI 接口
        │       └── ImmersiveActivity.kt   OpenXR 入口 Activity
        └── cpp/
            ├── CMakeLists.txt
            ├── openxr_app.{h,cpp}         OpenXR 会话 + 渲染线程
            ├── multiview_pipeline.{h,cpp} GL_TEXTURE_2D_ARRAY 多视图 FBO
            ├── vr180_mesh.{h,cpp}         VR180 半球网格生成（移植 iOS VideoTools）
            ├── video_texture.{h,cpp}      OES 纹理 + SurfaceTexture 桥
            ├── control_panel.{h,cpp}      浮动控制面板 + 手柄射线命中
            ├── jni_bridge.cpp             JNI 导出
            ├── gl_helpers.{h,cpp}         GL 编程 + 矩阵工具
            └── shaders.h                  内嵌 GLSL（vr180_sbs / panel）
```

## 首次构建

1. 进入工程目录：`cd /Users/dec/Documents/mine/vr-player`
2. 用 Android Studio "Open Existing Project" 打开。它会自动生成 `gradlew`、`gradle/wrapper/gradle-wrapper.jar` 和 `local.properties`（指向 SDK / NDK）。
3. 或者命令行里：
    ```bash
    gradle wrapper --gradle-version 8.9
    echo "sdk.dir=$ANDROID_HOME" > local.properties
    ./gradlew :app:assembleDebug
    ```
4. 安装：
    ```bash
    adb install -r app/build/outputs/apk/debug/app-debug.apk
    ```

## 运行

设备需在开发者模式下，并接通 USB 调试。

### 1. 本地视频

```bash
adb push your-vr180-sbs.mp4 /sdcard/Movies/
```

戴上 Quest 3 → 启动 "VR Player" → "选择视频文件" → 在 SAF 弹窗里挑刚 push 的文件 → 自动进入沉浸式空间。VR180 半球应贴合视场，左右眼各取 SBS 的左右半边。

### 2. 115 登录

主页点 "115 网盘" → 弹出登录提示 → 确定 → 内嵌 WebView 加载 [https://passportapi.115.com/open/authorize](https://passportapi.115.com/open/authorize) → 完成登录后被重定向到 `https://vrplayer.space?code=...&state=...`，应用即捕获并兑换 token。再次点击 "115 网盘" 直接进入文件列表。

### 3. 115 播放（原画 = 本地代理转发）

进入目录 → 选视频 → "选择视频质量" 弹窗 → 选 "原画"。logcat 里应能看到：

```
LocalHttpProxy: forward client=GET range=bytes=0-8388607 url=https://...
```

ExoPlayer 会按 8 MiB 块持续向本地代理请求，代理转发到 115 上游。

### 4. 沉浸空间内的控制面板

- 面板浮在头部前下方 1.5 m。
- 控制面板 3 秒无手柄输入后自动隐藏；隐藏后按任意 trigger / A / B / 摇杆即可重新显示。
- 用 Touch 控制器 trigger 按钮选择按钮：
  - `[-15s]` `[▶/⏸]` `[+15s]` 在面板左侧
  - 进度条占面板右半，命中位置即跳到对应比例
- 右手 A：播放/暂停；右手 B：退出播放模式。
- 左/右摇杆：左右长按连续滑动进度，按下播放/暂停，下推隐藏控制面板。
- 手柄射线命中由 OpenXR 的 aim pose 提供，面板光标只跟随左右手柄射线，不再使用头部凝视点控制。

## 调试常见问题

| 现象 | 排查 |
|---|---|
| 安装后图标只在 Quest "未知来源" 列表里 | 正常，VR-only 应用都在那。`adb shell am start -n space.vrplayer/.MainActivity` 也能起 |
| 进入沉浸空间后黑屏 | logcat `OpenXR call failed` 关键字；确认 Quest OS 已启用 OpenXR runtime（默认开） |
| 视频画面拉伸 / 上下颠倒 | 视频不是 SBS VR180。临时把 `NativeBridge.setStereoMode(StereoMode.Mono.raw)` 试一下 |
| 115 登录后立刻提示 "获取 token 失败" | 检查 `state` 是否一致；后端 `vocalremover.us/api/115/authCodeToToken/{code}` 是否可达 |
| 115 原画卡住 | logcat 看 `LocalHttpProxy` 是否有 `502 Bad Gateway`；签名失效就重新选清晰度 |
| 控制面板射线偏 | 确认 Quest 手柄已连接并处于追踪状态；logcat 查看 `controller bindings suggested` 是否包含 Touch Plus 或 Oculus Touch profile |

## 与 iOS PanPlayer 的对照

| iOS（visionOS） | Quest 3（Android / OpenXR） |
|---|---|
| RealityKit `ImmersiveSpace` | OpenXR session + GL 多视图 FBO |
| `MeshResource.generate` (sphere) | `vr180_mesh.cpp`（同算法） |
| `SBSMaterial.usda` + `GeometrySwitchCameraIndex` | `gl_ViewID_OVR` + UV 切分（fragment shader） |
| KSPlayer (FFmpeg) | ExoPlayer (Media3) → Surface(SurfaceTexture) → `samplerExternalOES` |
| `LocalHTTPProxy.swift`（NWListener） | `LocalHttpProxy.kt`（ServerSocket + OkHttp，钳制 8 MiB） |
| WebViewContainer + redirect 拦截 | `AuthWebViewActivity` + `WebViewClient.shouldOverrideUrlLoading` |
| `EncryptedSharedPreferences` 等价 = Keychain |

## 后续工作（v0 暂未做）

- HLS 多分辨率切换 UI（Media3 已自动 ABR；把 trackSelector 暴露给面板即可）
- 投影模式选择 UI（VR180 / VR360 / 2D 平面）
- 面板上的时间数值显示（需要内嵌位图字体或 stb_truetype）
- Spatial Video / MV-HEVC 拆流（Quest 3 解码器对此格式支持有限）

## 目录里的 `.claude/`

`/Users/dec/.claude/plans/clever-cuddling-neumann.md` 是这次实现遵循的计划。后续迭代可以继续读它。
