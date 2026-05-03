cmake_minimum_required(VERSION 3.22.1)

project("vrplayer" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# OpenXR loader provided by org.khronos.openxr:openxr_loader_for_android (via prefab)
find_package(OpenXR REQUIRED CONFIG)

add_library(${CMAKE_PROJECT_NAME} SHARED
    openxr_app.cpp
    multiview_pipeline.cpp
    vr180_mesh.cpp
    video_texture.cpp
    control_panel.cpp
    jni_bridge.cpp
    gl_helpers.cpp
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    XR_USE_GRAPHICS_API_OPENGL_ES=1
    XR_USE_PLATFORM_ANDROID=1
)

# Strict warnings, but tolerate generated GL/XR code
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
)

target_link_libraries(${CMAKE_PROJECT_NAME}
    OpenXR::openxr_loader
    EGL
    GLESv3
    android
    log
)
