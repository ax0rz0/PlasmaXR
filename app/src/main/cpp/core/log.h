#pragma once

#ifdef __ANDROID__
#include <android/log.h>

#define PXR_TAG "PlasmaXR"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, PXR_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, PXR_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, PXR_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, PXR_TAG, __VA_ARGS__)
#else
// Host builds (the compositor test harness in tools/wayland) log to stderr.
#include <cstdio>

#define PXR_LOG_(level, ...) (fprintf(stderr, level " " __VA_ARGS__), fputc('\n', stderr))
#define LOGD(...) PXR_LOG_("D", __VA_ARGS__)
#define LOGI(...) PXR_LOG_("I", __VA_ARGS__)
#define LOGW(...) PXR_LOG_("W", __VA_ARGS__)
#define LOGE(...) PXR_LOG_("E", __VA_ARGS__)
#endif
