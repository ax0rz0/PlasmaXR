#include "shell/app_model.h"

#include <algorithm>
#include <functional>

#include "core/log.h"
#include "shell/linux_apps.h"
#include "shell/native_apps.h"

namespace pxr {

namespace {
constexpr int kIconPx = 160;
constexpr const char* kSelfPackage = "org.plasmaxr.launcher";
constexpr size_t kMaxDefaultPins = 9;

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == '\n') {
            if (i > start) out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}
}  // namespace

void AppModel::init() {
    if (started_) return;
    started_ = true;
    stop_ = false;
    worker_ = std::thread([this] { workerMain(); });
    refresh();
}

void AppModel::shutdown() {
    if (!started_) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
    for (auto& kv : icons_) kv.second.destroy();
    icons_.clear();
    started_ = false;
}

void AppModel::refresh() {
    apps_.clear();
    for (const NativeAppDef& d : nativeApps()) {
        AppInfo a;
        a.pkg = d.id;
        a.label = d.title;
        a.native = true;
        a.glyph = d.glyph;
        a.color = d.color;
        apps_.push_back(std::move(a));
    }
    for (AppInfo& a : Jni::listApps()) {
        if (a.pkg != kSelfPackage) apps_.push_back(std::move(a));
    }
    size_t linuxCount = 0;
    static const unsigned palette[] = {0x3daee9, 0x1cdc9a, 0xf67400, 0x9b59b6, 0xda4453, 0x2980b9, 0x16a085};
    for (const LinuxApp& l : scanLinuxApps(Jni::linuxRoot())) {
        AppInfo a;
        a.pkg = "linux:" + l.id;
        a.activity = l.command;
        a.label = l.name;
        a.linuxApp = true;
        const std::string hay = l.id + " " + l.command;
        const bool term = hay.find("term") != std::string::npos || hay.rfind("foot", 0) == 0 ||
                          hay.find("konsole") != std::string::npos;
        a.glyph = term ? "terminal" : "apps";
        a.color = palette[std::hash<std::string>()(l.id) % (sizeof(palette) / sizeof(palette[0]))];
        apps_.push_back(std::move(a));
        ++linuxCount;
    }
    LOGI("AppModel: %zu apps (%zu native, %zu Linux)", apps_.size(), nativeApps().size(), linuxCount);
    loadPinned();
    ++revision_;
}

const AppInfo* AppModel::find(const std::string& pkg) const {
    for (const AppInfo& a : apps_) {
        if (a.pkg == pkg) return &a;
    }
    return nullptr;
}

void AppModel::loadPinned() {
    pinned_.clear();
    std::string saved = Jni::getPref("pinned", "");
    if (!saved.empty()) {
        for (const std::string& pkg : splitLines(saved)) {
            if (find(pkg)) pinned_.push_back(pkg);
        }
        return;
    }
    // First run: the native apps, the browser (if there is one), then user-installed apps, then
    // anything else.
    for (const NativeAppDef& d : nativeApps()) pinned_.push_back(d.id);
    for (const char* linuxDefault : {"linux:foot", "linux:steam", "linux:plasmaxr-steamvr"}) {
        if (find(linuxDefault)) pinned_.push_back(linuxDefault);
    }
    if (find("com.oculus.browser")) pinned_.push_back("com.oculus.browser");
    for (int pass = 0; pass < 2 && pinned_.size() < kMaxDefaultPins; ++pass) {
        for (const AppInfo& a : apps_) {
            if (pinned_.size() >= kMaxDefaultPins) break;
            if (pass == 0 && a.system) continue;
            if (std::find(pinned_.begin(), pinned_.end(), a.pkg) != pinned_.end()) continue;
            pinned_.push_back(a.pkg);
        }
    }
}

void AppModel::savePinned() {
    std::string joined;
    for (const std::string& p : pinned_) joined += p + "\n";
    Jni::setPref("pinned", joined);
}

bool AppModel::isPinned(const std::string& pkg) const {
    return std::find(pinned_.begin(), pinned_.end(), pkg) != pinned_.end();
}

void AppModel::togglePin(const std::string& pkg) {
    auto it = std::find(pinned_.begin(), pinned_.end(), pkg);
    if (it != pinned_.end()) pinned_.erase(it);
    else pinned_.push_back(pkg);
    savePinned();
    ++revision_;
}

std::vector<const AppInfo*> AppModel::pinnedApps() const {
    std::vector<const AppInfo*> out;
    for (const std::string& p : pinned_) {
        if (const AppInfo* a = find(p)) out.push_back(a);
    }
    return out;
}

void AppModel::noteLaunched(const std::string& pkg) {
    recents_.erase(std::remove(recents_.begin(), recents_.end(), pkg), recents_.end());
    recents_.insert(recents_.begin(), pkg);
    if (recents_.size() > 6) recents_.pop_back();
    ++revision_;
}

void AppModel::launch(const AppInfo& app) {
    if (app.native || app.linuxApp) return;  // opened by the shell itself (see Shell::launch)
    LOGI("Launching %s (%s)", app.label.c_str(), app.pkg.c_str());
    if (!Jni::launchApp(app.pkg, app.activity)) {
        LOGW("Launch failed for %s", app.pkg.c_str());
        return;
    }
    recents_.erase(std::remove(recents_.begin(), recents_.end(), app.pkg), recents_.end());
    recents_.insert(recents_.begin(), app.pkg);
    if (recents_.size() > 6) recents_.pop_back();
    ++revision_;
}

bool AppModel::isRecent(const std::string& pkg) const {
    return std::find(recents_.begin(), recents_.end(), pkg) != recents_.end();
}

const Texture* AppModel::icon(const AppInfo& app) {
    if (app.native || app.linuxApp) return nullptr;
    auto it = icons_.find(app.pkg);
    if (it != icons_.end()) return it->second.valid() ? &it->second : nullptr;
    if (requested_.insert(app.pkg).second) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            requests_.push_back(app);
        }
        wake_.notify_one();
    }
    return nullptr;
}

bool AppModel::pumpUploads(int maxPerFrame) {
    bool changed = false;
    for (int i = 0; i < maxPerFrame; ++i) {
        Decoded d;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (results_.empty()) break;
            d = std::move(results_.front());
            results_.pop_front();
        }
        icons_[d.pkg] = uploadRgba(d.pixels, true, true);
        changed = true;
    }
    if (changed) ++revision_;
    return changed;
}

void AppModel::workerMain() {
    for (;;) {
        AppInfo req;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stop_ || !requests_.empty(); });
            if (stop_) break;
            req = std::move(requests_.front());
            requests_.pop_front();
        }
        PixelBuffer pb = Jni::loadAppIcon(req.pkg, req.activity, kIconPx);
        std::lock_guard<std::mutex> lock(mutex_);
        results_.push_back({req.pkg, std::move(pb)});
    }
    Jni::detachThread();
}

}  // namespace pxr
