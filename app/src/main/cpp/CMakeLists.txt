cmake_minimum_required(VERSION 3.22.1)
project(plasmaxr LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Khronos OpenXR loader + headers, delivered as a Prefab package by the Gradle dependency.
find_package(OpenXR REQUIRED CONFIG)

# NativeActivity glue shipped with the NDK.
add_library(app_glue STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(app_glue PUBLIC ${ANDROID_NDK}/sources/android/native_app_glue)

file(GLOB_RECURSE PXR_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_library(plasmaxr SHARED ${PXR_SOURCES})
target_include_directories(plasmaxr PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(plasmaxr PRIVATE
    XR_USE_PLATFORM_ANDROID
    XR_USE_GRAPHICS_API_OPENGL_ES
    GL_GLEXT_PROTOTYPES
    EGL_EGLEXT_PROTOTYPES)
target_compile_options(plasmaxr PRIVATE
    -Wall -Wextra
    -Wno-unused-parameter
    -Wno-missing-field-initializers)
target_link_libraries(plasmaxr PRIVATE
    app_glue
    OpenXR::openxr_loader
    EGL
    GLESv3
    android
    log
    jnigraphics)

# Keep the NativeActivity entry point alive through the linker.
set_target_properties(plasmaxr PROPERTIES LINK_FLAGS "-u ANativeActivity_onCreate")
