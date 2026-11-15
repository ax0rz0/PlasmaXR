#include <android_native_app_glue.h>

#include <memory>

#include "app.h"

void android_main(android_app* app) {
    auto application = std::make_unique<pxr::App>(app);
    application->run();
}
